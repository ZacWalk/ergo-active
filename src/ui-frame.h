// ui-frame.h - libdesktop-backed dashboard presentation and event policy.
//
// The dashboard is a ui frame host: its charts and cards draw through
// render::draw_context, while the menu, slider and numeric editor are controls
// in the frame's element tree. Platform owns the native window and event loop.

#pragma once

#include "libdesktop/platform.h"
#include "libdesktop/ui.h"
#include "libdesktop/ui/controls.h"
#include "libdesktop/ui/elements.h"
#include "libdesktop/ui/menu_bar.h"

#include "app-services.h"
#include "data.h"
#include "dashboard-layout.h"
#include "dashboard-policy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class dashboard_element final : public ui::element
{
public:
	explicit dashboard_element(std::function<void(render::draw_context&, core::sizei)> paint)
		: _paint(std::move(paint))
	{
	}

	core::sizei measure(render::measure_context&, const int) const override { return {}; }

	void render(render::draw_context& dc, const core::pointi element_offset) const override
	{
		if (!_paint || bounds.is_empty()) return;
		const auto target = bounds.offset(element_offset);
		render::scoped_clip clip(dc, target);
		ui::offset_draw_context local(dc, target.top_left());
		_paint(local, bounds.extent());
	}

private:
	std::function<void(render::draw_context&, core::sizei)> _paint;
};

class main_frame final : public ui::element_host,
                         public std::enable_shared_from_this<main_frame>
{
public:
	static constexpr int MinDelay = 20;
	static constexpr int MaxDelay = 120;
	static constexpr uint32_t TimerId = 1;
	static constexpr double TimerPeriodSeconds = 10.0;
	static constexpr int TimerGap = usage_data::TimerGap;
	static constexpr int FlushIntervalTicks = 5 * TimerGap;

	main_frame(activity_store& store,
	           input_monitor& input,
	           uptime_service& clock,
	           session_monitor& session,
	           settings_store& settings,
	           clipboard_service& clipboard,
	           tray_service& tray)
		: _usage(&store), _daily(&store), _input(input), _clock(clock), _session(session),
		  _settings(settings), _clipboard(clipboard), _tray(tray),
		  _delay_control(MinDelay, MaxDelay, settings.load_delay(MinDelay, MaxDelay, 50))
	{
		_edit = make_edit();
		_slider = ui::make_slider(MinDelay, MaxDelay, [this](const int value, const bool tracking)
		{
			_delay_control.set_value(value);
			_slider_dragging = tracking;
			_editor_selected = false;
			sync_editor_from_delay();
			if (!tracking) _slider_dragging = false;
			if (_window) _window->invalidate();
		});
		_slider->set_pos(_delay_control.value());
		install_menu();

		_root = std::make_shared<ui::elements>();
		_canvas = std::make_shared<dashboard_element>([this](render::draw_context& dc, const core::sizei extent)
		{
			paint_dashboard(dc, extent);
		});
		// The canvas is first so the menu and controls draw over its presentation.
		_root->add(_canvas);
		_root->add(_menu_bar);
		_root->add(_slider);
		_root->add(_edit);

		if (!_usage.last_storage_error().empty())
			report_persistence_error("usage", _usage.last_storage_error());
		if (!_daily.last_storage_error().empty())
			report_persistence_error("history", _daily.last_storage_error());
	}

	~main_frame() override
	{
		if (!_shutdown_flushed) flush();
		_tray.uninstall();
	}

	main_frame(const main_frame&) = delete;
	main_frame& operator=(const main_frame&) = delete;

	void attach(const ui::frame_ptr& window)
	{
		_window = window;
		_window->window_text("ergo-active");
		_last_timer_ms = _clock.milliseconds();
		if (_usage.take_break_while_closed()) _daily.record_break();
		if (!_tray.install([this] { restore(); },
		                  [this](const core::pointi point, const bool keyboard_anchor)
		                  { show_tray_menu(point, keyboard_anchor); },
		                  [this] { flush(); }))
			core::log("ergo-active", "tray service unavailable: " + _tray.diagnostics());
	}

	void hide() { if (_window) _window->show(false); }

	void restore()
	{
		if (_window)
		{
			_window->show(true);
			_window->focus();
			_window->invalidate();
		}
	}

	void request_close(const bool skip_prompt = false)
	{
		// Menu and tray exits are explicit commands; only a user-initiated window close asks.
		if (skip_prompt) _skip_close_prompt = true;
		if (_window) _window->close();
	}

	void mark_session_ending()
	{
		// Windows asks can_exit during logoff, so remember the synchronous notification first.
		_session_ending = true;
		flush();
	}

	bool take_skip_close_prompt()
	{
		const bool result = _session_ending || _skip_close_prompt;
		_skip_close_prompt = false;
		return result;
	}

	bool dispatch_key(const char32_t key, const platform::key_state keys) const
	{
		return ui::dispatch_accelerator(_menus, key, keys);
	}

	bool editor_focused() const { return _edit && _edit->is_focused(); }

	void on_timer();
	void flush();

#if defined(ERGO_ACTIVE_TESTS)
	[[nodiscard]] std::string test_editor_text() const { return _edit ? _edit->window_text() : std::string{}; }
	[[nodiscard]] int test_delay_value() const { return _delay_control.value(); }
	[[nodiscard]] bool test_editor_has_selection() const { return _editor_selected; }
	[[nodiscard]] bool test_slider_dragging() const { return _slider_dragging; }
	void test_editor_changed(std::string_view value) { editor_changed(value); }
	void test_mark_session_ending() { mark_session_ending(); }
	[[nodiscard]] bool test_take_skip_close_prompt() { return take_skip_close_prompt(); }
	[[nodiscard]] int test_keyboard_ticks() const { return _daily.today().keyboard_ticks; }
	[[nodiscard]] int test_locked_ticks() const { return _daily.today().locked_ticks; }
	[[nodiscard]] int test_break_count() const { return _daily.today().break_count; }
#endif

	void on_window_layout(render::measure_context& mc, core::sizei extent,
	                      bool is_minimized) override;
	void on_window_paint(render::draw_context& dc) override { _root->render(dc, {}); }
	void visit_elements(const std::function<void(const ui::element_ptr&, core::pointi)>& visit) override
	{
		visit(_root, {});
	}

	void focus_changed(bool has_focus, const ui::control_ptr& child) override;
	void on_window_destroy() override { _window.reset(); }
	void invoke(commands) override {}
	bool is_command_checked(commands) override { return false; }
	void track_menu(core::recti bounds, const std::vector<ui::command_ptr>& commands) override
	{
		if (_window) _window->track_menu(bounds, commands);
	}
	const ui::frame_ptr frame() const override { return _window ? _window : ui::no_frame(); }
	const ui::frame_ptr owner() override { return frame(); }
	void invalidate_element(const ui::element_ptr&) override { if (_window) _window->invalidate(); }
	void invalidate(ui::host_invalid) override { if (_window) _window->invalidate(); }
	void controller_changed() override { if (_window) _window->invalidate(); }
	ui::controller_ptr controller_from_location(core::pointi loc, ui::hit_test_context& ctx) override
	{
		return _root->controller_from_location(
			std::static_pointer_cast<ui::element_host>(shared_from_this()), loc, {}, ctx);
	}

private:
	static constexpr core::color32 BackgroundColor = core::rgba(0x1A, 0x1A, 0x2E);
	static constexpr core::color32 SurfaceColor = core::rgba(0x24, 0x24, 0x3A);
	static constexpr core::color32 AccentColor = core::rgba(0x3A, 0x9B, 0xDC);
	static constexpr core::color32 TextColor = core::rgba(0xE0, 0xE0, 0xE0);
	static constexpr core::color32 DimTextColor = core::rgba(0x80, 0x80, 0x98);
	static constexpr core::color32 GraphBorderColor = core::rgba(0x3A, 0x3A, 0x4A);
	static constexpr core::color32 GreenColor = core::rgba(0x4E, 0xC9, 0x6F);
	static constexpr core::color32 YellowColor = core::rgba(0xE0, 0xA0, 0x20);
	static constexpr core::color32 RedColor = core::rgba(0xE0, 0x40, 0x40);
	static constexpr core::color32 ChartMouseColor = core::rgba(0xE8, 0x8D, 0x2A);
	static constexpr core::color32 PieOffColor = core::rgba(0x30, 0x30, 0x42);

	usage_data _usage;
	daily_stats _daily;
	input_monitor& _input;
	uptime_service& _clock;
	session_monitor& _session;
	settings_store& _settings;
	clipboard_service& _clipboard;
	tray_service& _tray;
	ui::frame_ptr _window;
	std::shared_ptr<ui::elements> _root;
	std::shared_ptr<dashboard_element> _canvas;
	ui::control_ptr _menu_bar;
	ui::trackbar_ptr _slider;
	ui::edit_ptr _edit;
	std::vector<ui::command_ptr> _menus;
	delay_control _delay_control;
	dashboard_layout _layout;
	core::sizei _extent;
	double _dpi = 1.0;
	uint64_t _last_timer_ms = 0;
	bool _slider_dragging = false;
	bool _editor_selected = false;
	bool _skip_close_prompt = false;
	bool _session_ending = false;
	int _last_balloon_ticks = 0;
	int _posture_tip_index = 0;
	int _ticks_since_flush = 0;
	bool _shutdown_flushed = false;
	std::string _reported_persistence_error;

	static int scale(double dpi, double value) { return dashboard_layout::scale(dpi, value); }
	static core::recti to_platform_rect(const dashboard_rect& rect)
	{
		return {rect.left, rect.top, rect.right, rect.bottom};
	}
	render::font_face font(int logical_size) const
	{
		render::font_desc desc;
		desc.family = "Calibri";
		desc.size = std::max(1, scale(_dpi, logical_size));
		return render::make_font(desc);
	}
	static core::color32 blend(core::color32 base, core::color32 tint, float amount)
	{
		return core::lerp(base, tint, std::clamp(amount, 0.0f, 1.0f));
	}
	static void fill_rounded(render::draw_context& draw, const dashboard_rect& rect,
	                         core::color32 color, int radius)
	{
		if (rect.width() <= 0 || rect.height() <= 0) return;
		draw.draw_rounded_rect(to_platform_rect(rect), core::color(color), radius);
	}

	static void draw_text_box(render::draw_context& draw, std::string_view text,
	                          render::font_face text_font, const dashboard_rect& rect,
	                          core::color32 text_color, core::color32 background,
	                          bool center = false, bool right = false, bool vertical_center = false)
	{
		if (text.empty() || rect.width() <= 0 || rect.height() <= 0) return;
		const auto extent = draw.measure_text(text, text_font, render::text_style::single_line,
		                                     std::numeric_limits<int>::max());
		int x = rect.left;
		if (center) x = rect.left + (rect.width() - extent.cx) / 2;
		else if (right) x = rect.right - extent.cx;
		int y = rect.top;
		if (vertical_center) y = rect.top + (rect.height() - extent.cy) / 2;
		const core::recti clip{x, y, x + std::max(1, extent.cx), y + std::max(1, extent.cy)};
		draw.draw_rect(clip, core::color(background));
		draw.draw_text(text, clip, text_font, render::text_style::single_line,
		               core::color(text_color), {});
	}

	ui::edit_ptr make_edit()
	{
		ui::edit_styles styles;
		styles.number = true;
		styles.align_center = true;
		styles.rounded_corners = false;
		styles.select_all_on_focus = true;
		styles.font = render::font_face::dialog;
		auto edit = ui::make_edit(styles, _delay_control.text(), [this](const std::string& value)
		{
			editor_changed(value);
		});
		edit->set_background(SurfaceColor);
		return edit;
	}

	void editor_changed(std::string_view value)
	{
		std::string filtered;
		for (const char c : value)
			if (c >= '0' && c <= '9') filtered.push_back(c);
		if (_edit && filtered != value) _edit->window_text(filtered);
		_delay_control.text_changed(filtered);
		_editor_selected = false;
		if (_window) _window->invalidate();
	}

	void install_menu()
	{
		auto hide_command = std::make_shared<ui::command>();
		hide_command->text = "Hide to tray";
		hide_command->invoke = [this] { hide(); };
		auto exit_command = std::make_shared<ui::command>();
		exit_command->text = "Exit";
		exit_command->invoke = [this] { request_close(true); };
		auto file = std::make_shared<ui::command>();
		file->text = "&File";
		file->menu = [hide_command, exit_command]
		{
			return std::vector<ui::command_ptr>{hide_command, exit_command};
		};
		_menus = {file};
		_menu_bar = ui::make_menu_bar(_menus);
	}

	void show_tray_menu(core::pointi screen_position, bool)
	{
		if (!_window) return;
		auto restore_command = std::make_shared<ui::command>();
		restore_command->text = "Restore";
		restore_command->invoke = [this] { restore(); };
		auto exit_command = std::make_shared<ui::command>();
		exit_command->text = "Exit";
		exit_command->invoke = [this] { request_close(true); };
		_window->track_menu({screen_position.x, screen_position.y,
		                     screen_position.x + 1, screen_position.y + 1},
		                    {restore_command, exit_command});
	}

	void sync_editor_from_delay() { if (_edit) _edit->window_text(_delay_control.text()); }

	void report_persistence_error(std::string_view kind, std::string_view diagnostic)
	{
		const std::string message = std::format("ergo-active {} persistence failed: {}", kind, diagnostic);
		if (message != _reported_persistence_error)
		{
			_reported_persistence_error = message;
			core::log("ergo-active", message);
		}
	}

	void show_break_balloon(int last_break)
	{
		static constexpr std::string_view tips[] = {
			"Roll your shoulders back and down", "Stretch your wrists - extend and flex",
			"Adjust your chair so feet are flat on the floor", "Blink deliberately - your eyes need moisture",
			"Stand up and stretch your legs", "Look away from the screen at something distant",
			"Relax your jaw and unclench your teeth", "Check your posture - sit up straight",
		};
		const auto message = std::format("You have not had a 3 minute break for {} or more minutes!", last_break);
		const auto tip = std::format("{}\n{}", message, tips[_posture_tip_index++ % std::size(tips)]);
		_tray.show_balloon("ergo-active - Take a Break!", tip, message);
		_last_balloon_ticks = 0;
	}

	void show_eye_reminder()
	{
		_tray.show_balloon("ergo-active - 20-20-20 Rule",
		                   "Look at something 20 feet away for 20 seconds.\nYour eyes will thank you!",
		                   "Eye break reminder");
		_last_balloon_ticks = 0;
	}

	static core::surface_ptr make_surface(int width, int height,
	                                      const std::vector<core::color32>& pixels)
	{
		if (width <= 0 || height <= 0) return {};
		const auto surface = std::make_shared<core::surface>();
		surface->alloc({width, height}, core::texture_format::RGB);
		for (int y = 0; y < height; ++y)
		{
			auto* const row = reinterpret_cast<core::color32*>(surface->pixels_line(y));
			for (int x = 0; x < width; ++x)
				row[x] = core::abgr(pixels[static_cast<size_t>(y) * width + x], 0);
		}
		return surface;
	}

	void paint_dashboard(render::draw_context& draw, core::sizei extent);
	void draw_graph(render::draw_context& draw, const dashboard_rect& panel);
	void draw_pie(render::draw_context& draw, const dashboard_rect& panel);
	void draw_pie_bitmap(render::draw_context& draw, const dashboard_rect& rect,
	                     const float* segments, const core::color32* colors, int count);
	static core::color32 pixel_lerp(core::color32 a, core::color32 b, int t256)
	{
		return core::lerp(a, b, std::clamp(static_cast<float>(t256) / 255.0f, 0.0f, 1.0f));
	}
};

inline void main_frame::on_window_layout(render::measure_context& mc, const core::sizei extent,
                                          const bool is_minimized)
{
	if (is_minimized) return;
	_extent = extent;
	_dpi = mc.scale_factor > 0.0 ? mc.scale_factor : 1.0;
	const auto menu_measure = _menu_bar->measure(mc, std::max(1, extent.cx));
	const int menu_height = std::max(scale(_dpi, 28.0), menu_measure.cy);
	_layout = dashboard_layout::measure(extent.cx, std::max(0, extent.cy - menu_height), _dpi);
	_root->bounds = core::recti(extent);
	_canvas->layout(mc, {0, menu_height, extent.cx, extent.cy});
	_menu_bar->layout(mc, {0, 0, extent.cx, menu_height});
	_slider->layout(mc, to_platform_rect(_layout.slider).offset({0, menu_height}));
	_edit->layout(mc, to_platform_rect(_layout.edit).offset({0, menu_height}));
	if (_window) _window->invalidate();
}

inline void main_frame::focus_changed(const bool has_focus, const ui::control_ptr& child)
{
	if (child.get() == _edit.get())
	{
		if (has_focus) _editor_selected = true;
		else
		{
			_delay_control.commit();
			sync_editor_from_delay();
			_editor_selected = false;
		}
	}
	if (_window) _window->invalidate();
}

inline void main_frame::flush()
{
	if (!_usage.flush()) report_persistence_error("usage", _usage.last_storage_error());
	if (!_daily.flush()) report_persistence_error("history", _daily.last_storage_error());
	if (!_settings.save_delay(_delay_control.value())) report_persistence_error("settings", _settings.diagnostics());
}

inline void main_frame::on_timer()
{
	const uint64_t now = _clock.milliseconds();
	const uint64_t elapsed = now >= _last_timer_ms ? now - _last_timer_ms : 0;
	const uint64_t timer_period_ms = static_cast<uint64_t>(TimerPeriodSeconds * 1000.0);
	const uint64_t periods = elapsed / timer_period_ms;
	if (periods == 0) return;
	_last_timer_ms += periods * timer_period_ms;
	if (periods >= 2)
	{
		const bool was_current_day = _daily.is_current_day();
		if (_usage.age(static_cast<int64_t>(periods) - 1).break_completed && was_current_day)
			_daily.record_break();
	}
	_session.poll();
	const auto input = _input.sample();
	const bool locked = _session.is_locked();
	const auto events = _usage.step(locked ? 0 : input.mouse, locked ? 0 : input.keyboard);
	++_last_balloon_ticks;
	_daily.record_tick({.keyboard = !locked && input.keyboard > 0,
	                    .mouse = !locked && input.mouse > 0,
	                    .locked = locked,
	                    .stretch_minutes = _usage.get_last_break_minutes(),
	                    .break_interval = _delay_control.value()});
	if (events.break_completed) _daily.record_break();
	if (events.micro_pause) _daily.record_micro_pause();

	const int delay = _delay_control.value();
	const int last_break = _usage.get_last_break_minutes();
	if (_last_balloon_ticks / TimerGap > 3 && _usage.is_active())
	{
		switch (choose_reminder(true, last_break, delay, _usage.should_show_eye_reminder()))
		{
		case reminder_kind::break_warning: show_break_balloon(last_break); break;
		case reminder_kind::eye_strain: show_eye_reminder(); break;
		case reminder_kind::none: break;
		}
	}
	_tray.update_urgency(urgency_level(last_break, delay));
	const int overdue = last_break - delay;
	_tray.update_tooltip(overdue > 0
		? std::format("ergo-active - Break overdue by {}m | Score: {}", overdue, _daily.today().score)
		: std::format("ergo-active - Next break in {}m | Score: {}", -overdue, _daily.today().score));
	if (++_ticks_since_flush >= FlushIntervalTicks)
	{
		_ticks_since_flush = 0;
		flush();
	}
	if (_window && _window->is_visible()) _window->invalidate();
}

inline void main_frame::paint_dashboard(render::draw_context& draw, const core::sizei extent)
{
	const core::recti bounds(extent);
	draw.clear(core::color(BackgroundColor));
	const int margin = static_cast<int>(_layout.margin);
	const int content_width = std::max(0, bounds.width() - margin * 2);
	const int title_height = scale(_dpi, 48.0);
	const int title_bottom = margin + title_height;
	const auto body_font = font(16);
	const auto small_font = font(9);
	const auto label_font = font(13);

	draw_text_box(draw, "ergo-active", font(28), {margin, 0, margin + content_width, title_bottom},
	              AccentColor, BackgroundColor, false, false, true);
	const auto& today = _daily.today();
	const core::color32 score_color = today.score < 50 ? RedColor : today.score < 75 ? YellowColor : GreenColor;
	draw_text_box(draw, std::format("Score: {}", today.score), label_font,
	              {margin, 0, margin + content_width, title_bottom}, score_color, BackgroundColor, false, true, true);

	const int cards_y = title_bottom + scale(_dpi, 6.0);
	const int card_height = scale(_dpi, 56.0);
	const int card_gap = scale(_dpi, 6.0);
	const int card_width = std::max(1, (content_width - card_gap * 3) / 4);
	const std::array<std::pair<std::string, core::color32>, 4> cards = {{
		{std::format("{}h {}m", today.active_minutes / 60, today.active_minutes % 60), TextColor},
		{std::format("{}m", _usage.get_minutes_until_warning(_delay_control.value())),
		 _usage.get_minutes_until_warning(_delay_control.value()) <= 5 ? RedColor :
		 _usage.get_minutes_until_warning(_delay_control.value()) <= 10 ? YellowColor : GreenColor},
		{std::format("{}", today.break_count), GreenColor},
		{std::format("{}m", today.longest_stretch),
		 today.longest_stretch > _delay_control.value() ? RedColor :
		 today.longest_stretch > _delay_control.value() * 3 / 4 ? YellowColor : TextColor},
	}};
	constexpr std::array<std::string_view, 4> card_labels = {"ACTIVE TODAY", "NEXT BREAK", "BREAKS", "MAX STRETCH"};
	for (int i = 0; i < 4; ++i)
	{
		const dashboard_rect card = {margin + i * (card_width + card_gap), cards_y,
		                             margin + i * (card_width + card_gap) + card_width, cards_y + card_height};
		fill_rounded(draw, card, SurfaceColor, scale(_dpi, 6.0));
		draw_text_box(draw, card_labels[i], small_font,
		              {card.left + scale(_dpi, 8.0), card.top, card.right - scale(_dpi, 8.0),
		               card.top + card.height() / 2}, DimTextColor, SurfaceColor, true, false, true);
		draw_text_box(draw, cards[i].first, body_font,
		              {card.left + scale(_dpi, 8.0), card.top + card.height() / 2,
		               card.right - scale(_dpi, 8.0), card.bottom - scale(_dpi, 6.0)},
		              cards[i].second, SurfaceColor, true, false, true);
	}

	const int content_top = cards_y + card_height + scale(_dpi, 8.0);
	const int content_bottom = std::max(content_top, static_cast<int>(_layout.bar_y) - scale(_dpi, 4.0));
	const int pie_width = std::max(scale(_dpi, 180.0), content_width * 28 / 100);
	const int gap = scale(_dpi, 8.0);
	const int graph_width = std::max(0, content_width - pie_width - gap);
	const dashboard_rect pie_panel{margin, content_top, margin + pie_width, content_bottom};
	const dashboard_rect graph_panel{margin + pie_width + gap, content_top,
	                                margin + pie_width + gap + graph_width, content_bottom};
	fill_rounded(draw, pie_panel, SurfaceColor, scale(_dpi, 6.0));
	fill_rounded(draw, graph_panel, SurfaceColor, scale(_dpi, 6.0));
	draw_pie(draw, pie_panel);
	draw_graph(draw, graph_panel);

	const dashboard_rect slider = _layout.slider;
	const dashboard_rect edit = _layout.edit;
	draw_text_box(draw, "Break interval (min):", small_font,
	              {margin, slider.top, static_cast<int>(_layout.label_width) + margin, slider.bottom},
	              DimTextColor, BackgroundColor, false, false, true);
	const auto micro = std::format("{} micro-pause{}", today.micro_pauses, today.micro_pauses == 1 ? "" : "s");
	draw_text_box(draw, micro, small_font,
	              {edit.right + scale(_dpi, 12.0), slider.top, edit.right + scale(_dpi, 150.0), slider.bottom},
	              DimTextColor, BackgroundColor, false, false, true);
	draw_text_box(draw, "ergo-active - break reminder", small_font,
	              {margin, static_cast<int>(_layout.bar_y), margin + content_width,
	               static_cast<int>(_layout.bar_y + _layout.bar_height)},
	              blend(DimTextColor, BackgroundColor, 0.3f), BackgroundColor, false, true, true);
}

inline void main_frame::draw_graph(render::draw_context& draw, const dashboard_rect& panel)
{
	const dashboard_rect rect{panel.left + scale(_dpi, 4.0), panel.top + scale(_dpi, 4.0),
	                          panel.right - scale(_dpi, 4.0), panel.bottom - scale(_dpi, 4.0)};
	const int width = rect.width();
	const int height = rect.height();
	const int n_width = std::min(width, usage_data::MaxUses);
	if (width <= 0 || height <= 0 || n_width <= 0) return;

	const int smooth_radius = std::clamp(n_width / 80, 3, 12);
	std::vector<float> mouse(n_width), keyboard(n_width), breaks(n_width);
	std::vector<float> temp_mouse(n_width), temp_keyboard(n_width);
	const auto* uses = _usage.get_uses();
	const auto* markers = _usage.get_break_markers();
	for (int i = 0; i < n_width; ++i)
	{
		float sum_mouse = 0.0f, sum_keyboard = 0.0f;
		int count = 0;
		for (int j = std::max(0, i - smooth_radius); j <= std::min(n_width - 1, i + smooth_radius); ++j)
		{
			sum_mouse += static_cast<float>(uses[j].mouse);
			sum_keyboard += static_cast<float>(uses[j].keyboard);
			++count;
		}
		temp_mouse[i] = sum_mouse / count;
		temp_keyboard[i] = sum_keyboard / count;
	}
	float max_value = 0.001f;
	for (int i = 0; i < n_width; ++i)
	{
		float sum_mouse = 0.0f, sum_keyboard = 0.0f, sum_break = 0.0f;
		int count = 0;
		for (int j = std::max(0, i - smooth_radius); j <= std::min(n_width - 1, i + smooth_radius); ++j)
		{
			sum_mouse += temp_mouse[j];
			sum_keyboard += temp_keyboard[j];
			sum_break += static_cast<float>(markers[j]);
			++count;
		}
		mouse[i] = sum_mouse / count;
		keyboard[i] = sum_keyboard / count;
		breaks[i] = sum_break / count;
		max_value = std::max(max_value, mouse[i] + keyboard[i]);
	}

	std::vector<core::color32> pixels(static_cast<size_t>(width) * height, BackgroundColor);
	const auto grid = blend(BackgroundColor, TextColor, 0.08f);
	for (int g = 1; g <= 4; ++g)
	{
		const int y = height - static_cast<int>(height * g / 4.0f);
		if (y >= 0 && y < height) std::fill_n(pixels.data() + static_cast<size_t>(y) * width, width, grid);
	}
	const core::color32 outline = blend(AccentColor, TextColor, 0.3f);
	for (int i = 0; i < n_width; ++i)
	{
		const int x = width - 1 - i;
		if (breaks[i] > 0.01f)
		{
			const auto band = core::lerp(BackgroundColor, GreenColor, breaks[i] * 0.25f);
			for (int y = 0; y < height; ++y) pixels[static_cast<size_t>(y) * width + x] = band;
		}
		const float mouse_value = mouse[i];
		const float keyboard_value = keyboard[i];
		const float total = mouse_value + keyboard_value;
		if (total <= 0.001f) continue;
		const int mouse_height = static_cast<int>(mouse_value * height / max_value);
		const int total_height = static_cast<int>(total * height / max_value);
		for (int dy = 0; dy < mouse_height && dy < height; ++dy)
			pixels[static_cast<size_t>(height - 1 - dy) * width + x] = ChartMouseColor;
		for (int dy = mouse_height; dy < total_height && dy < height; ++dy)
			pixels[static_cast<size_t>(height - 1 - dy) * width + x] = AccentColor;
		const int top = height - total_height;
		for (int dy = 0; dy < 2; ++dy)
			if (top + dy >= 0 && top + dy < height)
				pixels[static_cast<size_t>(top + dy) * width + x] = outline;
	}
	if (const auto surface = make_surface(width, height, pixels))
		render::draw_surface(draw, surface, to_platform_rect(rect));

	const auto axis_font = font(9);
	for (const int minutes : {30, 60, 120, 180})
	{
		const int tick = minutes * usage_data::TimerGap;
		if (tick >= n_width) continue;
		const int x = rect.right - tick;
		const std::string label = minutes >= 60 && minutes % 60 == 0
			? std::format("{}h", minutes / 60) : std::format("{}m", minutes);
		draw_text_box(draw, label, axis_font,
		              {x - scale(_dpi, 12.0), rect.bottom - scale(_dpi, 18.0),
		               x + scale(_dpi, 12.0), rect.bottom},
		              blend(TextColor, BackgroundColor, 0.5f), BackgroundColor, true, false, false);
	}
}

inline void main_frame::draw_pie(render::draw_context& draw, const dashboard_rect& panel)
{
	const int pad = scale(_dpi, 8.0);
	const int size = std::min(panel.width() - pad * 2, panel.height() - pad * 2 - scale(_dpi, 70.0));
	if (size <= scale(_dpi, 20.0)) return;
	const auto& today = _daily.today();
	const float segments[] = {
		static_cast<float>(today.keyboard_ticks), static_cast<float>(today.mouse_ticks),
		static_cast<float>(today.idle_ticks), static_cast<float>(today.locked_ticks),
		static_cast<float>(std::max(0, usage_data::MaxUses - today.keyboard_ticks - today.mouse_ticks -
		                                  today.idle_ticks - today.locked_ticks)),
	};
	const core::color32 colors[] = {AccentColor, ChartMouseColor, GreenColor, YellowColor, PieOffColor};
	const int x = panel.left + (panel.width() - size) / 2;
	const int y = panel.top + pad;
	draw_pie_bitmap(draw, {x, y, x + size, y + size}, segments, colors, 5);

	const auto legend_font = font(9);
	const std::string_view labels[] = {"Keyboard", "Mouse", "Machine On", "Locked", "Off"};
	float total = 0.0f;
	for (const float segment : segments) total += segment;
	const int row_height = scale(_dpi, 12.0);
	for (int i = 0; i < 5; ++i)
	{
		const int percent = total > 0.0f ? static_cast<int>(segments[i] / total * 100.0f + 0.5f) : 0;
		const int row_y = y + size + scale(_dpi, 6.0) + i * row_height;
		draw.draw_rect({panel.left + pad, row_y + scale(_dpi, 3.0),
		               panel.left + pad + scale(_dpi, 6.0), row_y + scale(_dpi, 9.0)},
		              core::color(colors[i]));
		draw_text_box(draw, std::format("{} {}%", labels[i], percent), legend_font,
		              {panel.left + pad + scale(_dpi, 10.0), row_y, panel.right - pad, row_y + row_height},
		              DimTextColor, SurfaceColor, false, false, true);
	}
}

inline void main_frame::draw_pie_bitmap(render::draw_context& draw, const dashboard_rect& rect,
                                         const float* segments, const core::color32* colors, const int count)
{
	const int width = rect.width();
	const int height = rect.height();
	const int radius = std::min(width, height) / 2 - 2;
	if (radius < 8) return;
	const int cx = width / 2;
	const int cy = height / 2;
	std::array<core::color32, 64> ring{};
	float total = 0.0f;
	for (int i = 0; i < count; ++i) total += segments[i];
	if (total <= 0.0f) total = 1.0f;
	int slot = 0;
	for (int i = 0; i < count && slot < 64; ++i)
	{
		int slots = static_cast<int>(std::round(segments[i] / total * 64.0f));
		if (i == count - 1) slots = 64 - slot;
		for (int j = 0; j < slots && slot < 64; ++j) ring[slot++] = colors[i];
	}
	if (slot > 0)
		while (slot < 64) ring[slot] = ring[slot++ - 1];

	const core::color32 background = SurfaceColor;
	const core::color32 center = SurfaceColor;
	const int outer_r1 = (radius - 1) * (radius - 1);
	const int outer_r2 = radius * radius;
	const int outer_diff = std::max(1, outer_r2 - outer_r1);
	const int inner_radius = radius / 2;
	const int inner_r1 = (inner_radius - 1) * (inner_radius - 1);
	const int inner_r2 = inner_radius * inner_radius;
	const int inner_diff = std::max(1, inner_r2 - inner_r1);
	constexpr float dither[4][4] = {
		{1.0f / 64, 9.0f / 64, 3.0f / 64, 11.0f / 64},
		{13.0f / 64, 5.0f / 64, 15.0f / 64, 7.0f / 64},
		{4.0f / 64, 12.0f / 64, 2.0f / 64, 10.0f / 64},
		{16.0f / 64, 8.0f / 64, 14.0f / 64, 6.0f / 64}
	};
	std::vector<core::color32> pixels(static_cast<size_t>(width) * height, background);
	for (int y = 0; y < height; ++y)
	{
		const int dy = y - cy;
		for (int x = 0; x < width; ++x)
		{
			const int dx = x - cx;
			const int distance = dx * dx + dy * dy;
			core::color32 color = background;
			if (distance < inner_r1)
				color = center;
			else if (distance < outer_r2)
			{
				const double index1 = (std::numbers::pi + std::atan2(static_cast<double>(dy),
				                                             static_cast<double>(dx))) /
				                      std::numbers::pi * 32.0;
				const double index2 = (std::numbers::pi + std::atan2(static_cast<double>(dy + 1),
				                                             static_cast<double>(dx + 1))) /
				                      std::numbers::pi * 32.0;
				const core::color32 c1 = ring[static_cast<int>(index1) % 64];
				const core::color32 c2 = ring[static_cast<int>(index2) % 64];
				color = c1 == c2 ? c1 : pixel_lerp(c1, c2,
					static_cast<int>(std::clamp(std::abs(index2 - index1), 0.0, 1.0) * 255));
				if (distance > outer_r1)
					color = pixel_lerp(color, background, static_cast<int>(std::clamp(
						static_cast<float>(distance - outer_r1) / outer_diff * 255.0f, 0.0f, 255.0f)));
				else if (distance < inner_r2)
					color = pixel_lerp(center, color, static_cast<int>(std::clamp(
						static_cast<float>(distance - inner_r1) / inner_diff * 255.0f, 0.0f, 255.0f)));
			}
			const float d = (static_cast<float>(distance) / outer_r2 + dither[x % 4][y % 4]) * 0.25f;
			const int lighten = static_cast<int>(d * 40.0f);
			color = core::rgba(std::min(255, static_cast<int>(core::get_r(color)) + lighten),
			                  std::min(255, static_cast<int>(core::get_g(color)) + lighten),
			                  std::min(255, static_cast<int>(core::get_b(color)) + lighten));
			pixels[static_cast<size_t>(y) * width + x] = color;
		}
	}
	if (const auto surface = make_surface(width, height, pixels))
		render::draw_surface(draw, surface, to_platform_rect(rect));
}
