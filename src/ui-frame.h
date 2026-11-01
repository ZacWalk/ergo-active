// ui-frame.h - libdesktop-backed dashboard presentation and event policy.
//
// The frame owns no HWND, HDC, HICON, registry handle or Win32 message.  It
// receives pf:: events, draws through pf::draw_context, and delegates activity,
// persistence, session and tray work to the neutral services.

#pragma once

#include "platform.h"
#include "ui/widgets.h"

#include "app-services.h"
#include "data.h"
#include "dashboard-layout.h"
#include "dashboard-policy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class main_frame final : public pf::frame_reactor
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
		: _usage(&store),
		  _daily(&store),
		  _input(input),
		  _clock(clock),
		  _session(session),
		  _settings(settings),
		  _clipboard(clipboard),
		  _tray(tray),
		  _delay_control(MinDelay, MaxDelay, settings.load_delay(MinDelay, MaxDelay, 50))
	{
		_edit.edit.text = std::string(_delay_control.text());
		_edit.edit.cursor_pos = static_cast<int>(_edit.edit.text.size());
		_edit.edit.sel_anchor = _edit.edit.cursor_pos;
		if (!_usage.last_storage_error().empty())
			report_persistence_error("usage", _usage.last_storage_error());
		if (!_daily.last_storage_error().empty())
			report_persistence_error("history", _daily.last_storage_error());
	}

	~main_frame() override
	{
		if (!_shutdown_flushed)
			flush();
		_tray.uninstall();
	}

	main_frame(const main_frame&) = delete;
	main_frame& operator=(const main_frame&) = delete;

	void install_menu(const pf::window_frame_ptr& window)
	{
		pf::menu_command file;
		file.text = "&File";
		file.children.emplace_back("Hide to tray", 1001,
		                           [this] { hide(); });
		file.children.emplace_back("Exit", 1002,
		                           [this] { request_close(); });
		window->set_menu({std::move(file)});
	}

	void hide()
	{
		if (_window)
			_window->show(false);
	}

	void restore()
	{
		if (_window)
		{
			_window->show(true);
			_window->set_focus();
			_window->invalidate();
		}
	}

	void request_close()
	{
		if (_window)
			_window->close();
	}

#if defined(ERGO_ACTIVE_TESTS)
	[[nodiscard]] std::string test_editor_text() const { return _edit.edit.text; }
	[[nodiscard]] int test_delay_value() const { return _delay_control.value(); }
	[[nodiscard]] bool test_editor_has_selection() const { return _edit.edit.has_selection(); }
	[[nodiscard]] bool test_slider_dragging() const { return _slider_dragging; }
	[[nodiscard]] int test_keyboard_ticks() const { return _daily.today().keyboard_ticks; }
	[[nodiscard]] int test_locked_ticks() const { return _daily.today().locked_ticks; }
	[[nodiscard]] int test_break_count() const { return _daily.today().break_count; }
#endif

	void show_tray_menu(const pf::ipoint screen_position, const bool)
	{
		if (!_window)
			return;

		pf::menu_command restore_command{"Restore", 2001, [this] { restore(); }};
		pf::menu_command exit_command{"Exit", 2002, [this] { request_close(); }};
		_window->show_popup_menu({std::move(restore_command), std::move(exit_command)}, screen_position);
	}

	uint32_t handle_message(pf::window_frame_ptr window,
	                        const pf::message_type message,
	                        const pf::message_params& params) override
	{
		switch (message)
		{
		case pf::message_type::create:
			return on_create(window);

		case pf::message_type::destroy:
			on_destroy();
			return 0;

		case pf::message_type::erase_background:
			return 1;

		case pf::message_type::timer:
			if (_edit.on_timer(params.timer_id))
			{
				window->invalidate();
				return 0;
			}
			return params.timer_id == TimerId ? on_timer() : 0;

		case pf::message_type::close:
			return on_close();

		case pf::message_type::dpi_changed:
			_dpi = params.dpi_scale > 0.0 ? params.dpi_scale : 1.0;
			recompute_layout(window);
			return 0;

		case pf::message_type::set_focus:
		case pf::message_type::kill_focus:
			window->invalidate();
			return 0;

		case pf::message_type::command:
		case pf::message_type::sys_color_change:
		case pf::message_type::init_dialog:
		case pf::message_type::drop_files:
			return 0;
		}

		return 0;
	}

	uint32_t handle_mouse(pf::window_frame_ptr window,
	                      const pf::mouse_message_type message,
	                      const pf::mouse_params& params) override
	{
		switch (message)
		{
		case pf::mouse_message_type::left_button_down:
			window->set_focus();
			if (_layout.slider.contains(params.point.x, params.point.y))
			{
				set_focus(focus_target::slider);
				_slider_dragging = true;
				window->set_capture();
				set_slider_from_x(params.point.x);
				return 1;
			}

			if (_layout.edit.contains(params.point.x, params.point.y))
			{
				set_focus(focus_target::edit);
				set_edit_cursor(params.point.x);
				return 1;
			}

			set_focus(focus_target::none);
			return 0;

		case pf::mouse_message_type::left_button_up:
			if (_slider_dragging)
			{
				_slider_dragging = false;
				window->release_capture();
				return 1;
			}
			return 0;

		case pf::mouse_message_type::mouse_move:
			if (_slider_dragging)
			{
				set_slider_from_x(params.point.x);
				return 1;
			}
			return 0;

		case pf::mouse_message_type::set_cursor:
			return 0;

		case pf::mouse_message_type::right_button_down:
		case pf::mouse_message_type::right_button_up:
		case pf::mouse_message_type::middle_button_down:
		case pf::mouse_message_type::middle_button_up:
		case pf::mouse_message_type::left_button_dbl_clk:
		case pf::mouse_message_type::mouse_wheel:
		case pf::mouse_message_type::mouse_leave:
		case pf::mouse_message_type::mouse_activate:
		case pf::mouse_message_type::context_menu:
			return 0;
		}
		return 0;
	}

	uint32_t handle_keyboard(pf::window_frame_ptr window,
	                         const pf::keyboard_message_type message,
	                         const pf::keyboard_params& params) override
	{
		if (message == pf::keyboard_message_type::char_input)
		{
			if (_focus != focus_target::edit)
				return 0;

			if (params.ch != 0x08 && (params.ch < U'0' || params.ch > U'9'))
				return 1;

			if (_edit.on_char(window, params.ch))
			{
				_delay_control.text_changed(_edit.edit.text);
				window->invalidate();
			}
			return 1;
		}

		if (message != pf::keyboard_message_type::key_down)
			return 0;

		if (params.vk == pf::platform_key::Tab)
		{
			const bool backwards = window->is_key_down(pf::platform_key::Shift);
			move_focus(backwards);
			return 1;
		}

		if (_focus == focus_target::slider)
		{
			if (params.vk == pf::platform_key::Left)
			{
				_delay_control.nudge(-1);
				sync_editor_from_delay();
				window->invalidate();
				return 1;
			}
			if (params.vk == pf::platform_key::Right)
			{
				_delay_control.nudge(1);
				sync_editor_from_delay();
				window->invalidate();
				return 1;
			}
			if (params.vk == pf::platform_key::Home)
			{
				_delay_control.set_value(MinDelay);
				sync_editor_from_delay();
				window->invalidate();
				return 1;
			}
			if (params.vk == pf::platform_key::End)
			{
				_delay_control.set_value(MaxDelay);
				sync_editor_from_delay();
				window->invalidate();
				return 1;
			}
		}

		if (_focus == focus_target::edit)
		{
			const bool control = window->is_key_down(pf::platform_key::Control);
			if (control && params.vk == 'A')
			{
				_edit.edit.select_all();
				_edit.reset_caret(window);
				window->invalidate();
				return 1;
			}
			if (control && params.vk == 'C')
			{
				if (_edit.edit.has_selection())
					_clipboard.set_text(_edit.edit.selected_text());
				return 1;
			}
			if (control && params.vk == 'X')
			{
				if (_edit.edit.has_selection() &&
				    _clipboard.set_text(_edit.edit.selected_text()))
				{
					_edit.edit.delete_selection();
					_delay_control.text_changed(_edit.edit.text);
				}
				_edit.reset_caret(window);
				window->invalidate();
				return 1;
			}
			if (control && params.vk == 'V')
			{
				const auto clipboard = _clipboard.text();
				std::string digits;
				for (const char c : clipboard)
					if (c >= '0' && c <= '9')
						digits += c;
				if (!digits.empty())
				{
					_edit.edit.insert_at_cursor(digits);
					_delay_control.text_changed(_edit.edit.text);
				}
				_edit.reset_caret(window);
				window->invalidate();
				return 1;
			}

			bool modified = false;
			if (_edit.on_key_down(window, params.vk, modified))
			{
				if (modified)
					_delay_control.text_changed(_edit.edit.text);
				window->invalidate();
				return 1;
			}
		}

		return 0;
	}

	void handle_paint(pf::window_frame_ptr& window, pf::draw_context& draw) override
	{
		auto bounds = window->get_client_rect();
		if (bounds.width() <= 0 || bounds.height() <= 0)
			bounds = {0, 0, _extent.cx, _extent.cy};
		paint(bounds, draw);
	}

	void handle_size(pf::window_frame_ptr& window,
	                 const pf::isize extent,
	                 pf::measure_context&) override
	{
		_extent = extent;
		_layout = dashboard_layout::measure(extent.cx, extent.cy, _dpi);
		window->invalidate();
	}

private:
	enum class focus_target
	{
		none,
		slider,
		edit,
	};

	static constexpr pf::color_t BackgroundColor{0x1A, 0x1A, 0x2E};
	static constexpr pf::color_t SurfaceColor{0x24, 0x24, 0x3A};
	static constexpr pf::color_t AccentColor{0x3A, 0x9B, 0xDC};
	static constexpr pf::color_t TextColor{0xE0, 0xE0, 0xE0};
	static constexpr pf::color_t DimTextColor{0x80, 0x80, 0x98};
	static constexpr pf::color_t GraphBorderColor{0x3A, 0x3A, 0x4A};
	static constexpr pf::color_t GreenColor{0x4E, 0xC9, 0x6F};
	static constexpr pf::color_t YellowColor{0xE0, 0xA0, 0x20};
	static constexpr pf::color_t RedColor{0xE0, 0x40, 0x40};
	static constexpr pf::color_t ChartMouseColor{0xE8, 0x8D, 0x2A};
	static constexpr pf::color_t PieOffColor{0x30, 0x30, 0x42};

	usage_data _usage;
	daily_stats _daily;
	input_monitor& _input;
	uptime_service& _clock;
	session_monitor& _session;
	settings_store& _settings;
	clipboard_service& _clipboard;
	tray_service& _tray;
	pf::window_frame_ptr _window;
	pf::ui::edit_box_widget _edit;
	delay_control _delay_control;
	dashboard_layout _layout;
	pf::isize _extent;
	double _dpi = 1.0;
	uint64_t _last_timer_ms = 0;
	focus_target _focus = focus_target::none;
	bool _slider_dragging = false;
	int _last_balloon_ticks = 0;
	int _posture_tip_index = 0;
	int _ticks_since_flush = 0;
	bool _shutdown_flushed = false;
	std::string _reported_persistence_error;

	static int scale(const double dpi, const double value)
	{
		return dashboard_layout::scale(dpi, value);
	}

	pf::font font(const int logical_size) const
	{
		return {std::max(1, scale(_dpi, logical_size)), pf::font_name::calibri};
	}

	static pf::color_t blend(const pf::color_t base, const pf::color_t tint, const float amount)
	{
		const float t = std::clamp(amount, 0.0f, 1.0f);
		const auto channel = [t](const uint8_t a, const uint8_t b)
		{
			return static_cast<uint8_t>(std::lround(static_cast<float>(a) * (1.0f - t) +
			                                        static_cast<float>(b) * t));
		};
		return {channel(base.r, tint.r), channel(base.g, tint.g), channel(base.b, tint.b)};
	}

	static uint32_t bitmap_pixel(const pf::color_t color)
	{
		// DIB pixels are laid out B,G,R in memory; this is the numeric form
		// expected by SetDIBitsToDevice in libdesktop's Win32 backend.
		return (static_cast<uint32_t>(color.r) << 16) |
			(static_cast<uint32_t>(color.g) << 8) | color.b;
	}

	static dashboard_rect to_dashboard_rect(const pf::irect& rect)
	{
		return {rect.left, rect.top, rect.right, rect.bottom};
	}

	static pf::irect to_platform_rect(const dashboard_rect& rect)
	{
		return {rect.left, rect.top, rect.right, rect.bottom};
	}

	static void fill_rounded(pf::draw_context& draw, const dashboard_rect& rect,
	                         const pf::color_t color, const int radius)
	{
		if (rect.width() <= 0 || rect.height() <= 0)
			return;

		const int r = std::min({radius, rect.width() / 2, rect.height() / 2});
		draw.fill_solid_rect(rect.left + r, rect.top, rect.width() - 2 * r, rect.height(), color);
		draw.fill_solid_rect(rect.left, rect.top + r, rect.width(), rect.height() - 2 * r, color);
		draw.fill_ellipse(rect.left, rect.top, 2 * r, 2 * r, color);
		draw.fill_ellipse(rect.right - 2 * r, rect.top, 2 * r, 2 * r, color);
		draw.fill_ellipse(rect.left, rect.bottom - 2 * r, 2 * r, 2 * r, color);
		draw.fill_ellipse(rect.right - 2 * r, rect.bottom - 2 * r, 2 * r, 2 * r, color);
	}

	static void draw_text_box(pf::draw_context& draw,
	                          const std::string_view text,
	                          const pf::font& text_font,
	                          const dashboard_rect& rect,
	                          const pf::color_t text_color,
	                          const pf::color_t background,
	                          const bool center = false,
	                          const bool right = false,
	                          const bool vertical_center = false)
	{
		if (text.empty() || rect.width() <= 0 || rect.height() <= 0)
			return;

		const auto extent = draw.measure_text(text, text_font);
		int x = rect.left;
		if (center)
			x = rect.left + (rect.width() - extent.cx) / 2;
		else if (right)
			x = rect.right - extent.cx;
		int y = rect.top;
		if (vertical_center)
			y = rect.top + (rect.height() - extent.cy) / 2;

		const pf::irect clip{x, y, x + std::max(1, extent.cx), y + std::max(1, extent.cy)};
		draw.draw_text(x, y, clip, text, text_font, text_color, background);
	}

	void set_focus(const focus_target target)
	{
		if (_focus == focus_target::edit && target != focus_target::edit)
		{
			_delay_control.commit();
			_edit.edit.text = std::string(_delay_control.text());
			_edit.edit.cursor_pos = static_cast<int>(_edit.edit.text.size());
			_edit.edit.sel_anchor = _edit.edit.cursor_pos;
		}

		_focus = target;
		if (_window)
		{
			_edit.update_focus(_window, _focus == focus_target::edit);
			_window->invalidate();
		}
	}

	void move_focus(const bool backwards)
	{
		if (backwards)
		{
			if (_focus == focus_target::edit)
				set_focus(focus_target::slider);
			else if (_focus == focus_target::slider)
				set_focus(focus_target::none);
			else
				set_focus(focus_target::edit);
		}
		else
		{
			if (_focus == focus_target::none)
				set_focus(focus_target::slider);
			else if (_focus == focus_target::slider)
				set_focus(focus_target::edit);
			else
				set_focus(focus_target::none);
		}

		if (_focus == focus_target::edit)
		{
			_edit.edit.select_all();
			if (_window)
				_edit.reset_caret(_window);
		}
	}

	void sync_editor_from_delay()
	{
		_edit.edit.text = std::string(_delay_control.text());
		_edit.edit.cursor_pos = static_cast<int>(_edit.edit.text.size());
		_edit.edit.sel_anchor = _edit.edit.cursor_pos;
	}

	void set_edit_cursor(const int mouse_x)
	{
		if (!_window)
			return;

		const auto measure = _window->create_measure_context();
		if (!measure)
			return;

		const auto edit_font = font(13);
		const int text_width = measure->measure_text(_edit.edit.text, edit_font).cx;
		const int text_x = _layout.edit.right - scale(_dpi, 5.0) - text_width;
		const int relative = mouse_x - text_x;
		if (relative <= 0)
		{
			_edit.edit.cursor_pos = 0;
			_edit.edit.sel_anchor = 0;
			return;
		}

		int best = static_cast<int>(_edit.edit.text.size());
		int position = 0;
		while (position < static_cast<int>(_edit.edit.text.size()))
		{
			const int next = pf::utf8_next(_edit.edit.text, position);
			const int before = measure->measure_text(
				std::string_view(_edit.edit.text).substr(0, position), edit_font).cx;
			const int after = measure->measure_text(
				std::string_view(_edit.edit.text).substr(0, next), edit_font).cx;
			if (relative < (before + after) / 2)
			{
				best = position;
				break;
			}
			position = next;
		}
		_edit.edit.cursor_pos = best;
		_edit.edit.sel_anchor = best;
	}

	void set_slider_from_x(const int x)
	{
		const int inset = scale(_dpi, 8.0);
		const int left = _layout.slider.left + inset;
		const int right = _layout.slider.right - inset;
		const int usable = std::max(1, right - left);
		const double ratio = std::clamp(static_cast<double>(x - left) / usable, 0.0, 1.0);
		const int value = MinDelay + static_cast<int>(std::lround(ratio * (MaxDelay - MinDelay)));
		_delay_control.set_value(value);
		sync_editor_from_delay();
		if (_window)
			_window->invalidate();
	}

	void recompute_layout(const pf::window_frame_ptr& window)
	{
		_extent = {window->get_client_rect().width(), window->get_client_rect().height()};
		_layout = dashboard_layout::measure(_extent.cx, _extent.cy, _dpi);
		window->invalidate();
	}

	uint32_t on_create(const pf::window_frame_ptr& window)
	{
		_window = window;
		_window->set_text("ergo-active");
		_dpi = window->get_dpi_scale();
		if (!(_dpi > 0.0))
			_dpi = 1.0;
		auto placement = _window->get_placement();
		const auto screen = pf::platform_screen_size();
		const int width = placement.normal_bounds.width();
		const int height = placement.normal_bounds.height();
		if (width > 0 && height > 0)
		{
			placement.normal_bounds = {
				(screen.cx - width) / 2, (screen.cy - height) / 2,
				(screen.cx + width) / 2, (screen.cy + height) / 2};
			_window->set_placement(placement);
		}
		recompute_layout(window);

		_last_timer_ms = _clock.milliseconds();
		_window->set_timer(TimerId, static_cast<uint32_t>(TimerPeriodSeconds * 1000.0));

		if (_usage.take_break_while_closed())
			_daily.record_break();

		if (!_tray.install([this] { restore(); },
		              [this](const pf::ipoint point, const bool keyboard_anchor)
		              {
		              show_tray_menu(point, keyboard_anchor);
		              },
		              [this] { flush(); }))
			pf::debug_trace("ergo-active tray service unavailable: " + _tray.diagnostics() + "\n");
		return 0;
	}

	void on_destroy()
	{
		if (_window)
			_window->kill_timer(TimerId);
		flush();
		_shutdown_flushed = true;
		_tray.uninstall();
		_window.reset();
	}

	void flush()
	{
		if (!_usage.flush())
			report_persistence_error("usage", _usage.last_storage_error());
		if (!_daily.flush())
			report_persistence_error("history", _daily.last_storage_error());
		if (!_settings.save_delay(_delay_control.value()))
			report_persistence_error("settings", _settings.diagnostics());
	}

	void report_persistence_error(const std::string_view kind, const std::string_view diagnostic)
	{
		const std::string message = std::format("ergo-active {} persistence failed: {}",
		                                        kind, diagnostic);
		if (message != _reported_persistence_error)
		{
			_reported_persistence_error = message;
			pf::debug_trace(message + "\n");
		}
	}

	uint32_t on_close()
	{
		if (!_window)
			return 0;

		const int answer = _window->message_box(
			"When hidden, ergo-active will continue monitoring activity and remind you to take breaks.\n\n"
			"Choose Yes to hide, No to exit, or Cancel to keep the dashboard open.",
			"ergo-active",
			pf::msg_box_style::yes_no_cancel | pf::msg_box_style::icon_question);
		if (answer == pf::msg_box_result::yes)
			hide();
		else if (answer == pf::msg_box_result::no)
			_window->close();
		return 0;
	}

	uint32_t on_timer()
	{
		const uint64_t now = _clock.milliseconds();
		const uint64_t elapsed = now >= _last_timer_ms ? now - _last_timer_ms : 0;
		uint64_t periods = elapsed / static_cast<uint64_t>(TimerPeriodSeconds * 1000.0);
		_last_timer_ms = now;
		if (periods == 0)
			periods = 1;

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

		_daily.record_tick({
			.keyboard = !locked && input.keyboard > 0,
			.mouse = !locked && input.mouse > 0,
			.locked = locked,
			.stretch_minutes = _usage.get_last_break_minutes(),
			.break_interval = _delay_control.value()
		});

		if (events.break_completed)
			_daily.record_break();
		if (events.micro_pause)
			_daily.record_micro_pause();

		const int delay = _delay_control.value();
		const int last_break = _usage.get_last_break_minutes();
		if (_last_balloon_ticks / TimerGap > 3 && _usage.is_active())
		{
			switch (choose_reminder(true, last_break, delay, _usage.should_show_eye_reminder()))
			{
			case reminder_kind::break_warning:
				show_break_balloon(last_break);
				break;
			case reminder_kind::eye_strain:
				show_eye_reminder();
				break;
			case reminder_kind::none:
				break;
			}
		}

		_tray.update_urgency(urgency_level(last_break, delay));
		const int overdue = last_break - delay;
		const std::string tooltip = overdue > 0
			? std::format("ergo-active - Break overdue by {}m | Score: {}",
			              overdue, _daily.today().score)
			: std::format("ergo-active - Next break in {}m | Score: {}",
			              -overdue, _daily.today().score);
		_tray.update_tooltip(tooltip);

		if (++_ticks_since_flush >= FlushIntervalTicks)
		{
			_ticks_since_flush = 0;
			flush();
		}

		if (_window && _window->is_visible())
			_window->invalidate();
		return 0;
	}

	void show_break_balloon(const int last_break)
	{
		static constexpr std::string_view tips[] = {
			"Roll your shoulders back and down",
			"Stretch your wrists - extend and flex",
			"Adjust your chair so feet are flat on the floor",
			"Blink deliberately - your eyes need moisture",
			"Stand up and stretch your legs",
			"Look away from the screen at something distant",
			"Relax your jaw and unclench your teeth",
			"Check your posture - sit up straight",
		};
		const auto message = std::format(
			"You have not had a 3 minute break for {} or more minutes!", last_break);
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

	void paint(const pf::irect& bounds, pf::draw_context& draw)
	{
		const dashboard_rect client = to_dashboard_rect(bounds);
		draw.fill_solid_rect(bounds, BackgroundColor);

		const int margin = static_cast<int>(_layout.margin);
		const int content_width = std::max(0, bounds.width() - margin * 2);
		const int title_height = scale(_dpi, 48.0);
		const int title_bottom = margin + title_height;
		const auto body_font = font(16);
		const auto small_font = font(9);
		const auto label_font = font(13);

		draw_text_box(draw, "ergo-active", font(28),
		              {margin, 0, margin + content_width, title_bottom},
		              AccentColor, BackgroundColor, false, false, true);

		const auto& today = _daily.today();
		const pf::color_t score_color = today.score < 50 ? RedColor :
			today.score < 75 ? YellowColor : GreenColor;
		draw_text_box(draw, std::format("Score: {}", today.score), label_font,
		              {margin, 0, margin + content_width, title_bottom},
		              score_color, BackgroundColor, false, true, true);

		const int cards_y = title_bottom + scale(_dpi, 6.0);
		const int card_height = scale(_dpi, 56.0);
		const int card_gap = scale(_dpi, 6.0);
		const int card_width = std::max(1, (content_width - card_gap * 3) / 4);
		const std::array<std::pair<std::string, pf::color_t>, 4> cards = {{
			{std::format("{}h {}m", today.active_minutes / 60, today.active_minutes % 60), TextColor},
			{std::format("{}m", _usage.get_minutes_until_warning(_delay_control.value())),
			 _usage.get_minutes_until_warning(_delay_control.value()) <= 5 ? RedColor :
			 _usage.get_minutes_until_warning(_delay_control.value()) <= 10 ? YellowColor : GreenColor},
			{std::format("{}", today.break_count), GreenColor},
			{std::format("{}m", today.longest_stretch),
			 today.longest_stretch > _delay_control.value() ? RedColor :
			 today.longest_stretch > _delay_control.value() * 3 / 4 ? YellowColor : TextColor},
		}};
		constexpr std::array<std::string_view, 4> card_labels = {
			"ACTIVE TODAY", "NEXT BREAK", "BREAKS", "MAX STRETCH"};

		for (int i = 0; i < 4; ++i)
		{
			const dashboard_rect card = {
				margin + i * (card_width + card_gap), cards_y,
				margin + i * (card_width + card_gap) + card_width, cards_y + card_height};
			fill_rounded(draw, card, SurfaceColor, scale(_dpi, 6.0));
			draw_text_box(draw, card_labels[i], small_font,
			              {card.left + scale(_dpi, 8.0), card.top,
			               card.right - scale(_dpi, 8.0), card.top + card.height() / 2},
			              DimTextColor, SurfaceColor, true, false, true);
			draw_text_box(draw, cards[i].first, body_font,
			              {card.left + scale(_dpi, 8.0), card.top + card.height() / 2,
			               card.right - scale(_dpi, 8.0), card.bottom - scale(_dpi, 6.0)},
			              cards[i].second, SurfaceColor, true, false, true);
		}

		const int content_top = cards_y + card_height + scale(_dpi, 8.0);
		const int content_bottom = std::max(content_top,
			static_cast<int>(_layout.bar_y) - scale(_dpi, 4.0));
		const int content_height = content_bottom - content_top;
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
		draw_slider(draw, slider);
		draw_edit(draw, edit);

		const auto micro = std::format("{} micro-pause{}",
		                               today.micro_pauses, today.micro_pauses == 1 ? "" : "s");
		draw_text_box(draw, micro, small_font,
		              {edit.right + scale(_dpi, 12.0), slider.top,
		               edit.right + scale(_dpi, 150.0), slider.bottom},
		              DimTextColor, BackgroundColor, false, false, true);
		draw_text_box(draw, "ergo-active - break reminder", small_font,
		              {margin, static_cast<int>(_layout.bar_y), margin + content_width,
		               static_cast<int>(_layout.bar_y + _layout.bar_height)},
		              blend(DimTextColor, BackgroundColor, 0.3f), BackgroundColor,
		              false, true, true);
		(void)client;
	}

	void draw_slider(pf::draw_context& draw, const dashboard_rect& rect)
	{
		const int inset = scale(_dpi, 8.0);
		const int left = rect.left + inset;
		const int right = rect.right - inset;
		const int center_y = rect.top + rect.height() / 2;
		const int track_height = std::max(2, scale(_dpi, 4.0));
		const int track_top = center_y - track_height / 2;
		const int usable = std::max(1, right - left);
		const double ratio = static_cast<double>(_delay_control.value() - MinDelay) /
		                     static_cast<double>(MaxDelay - MinDelay);
		const int thumb = left + static_cast<int>(std::lround(ratio * usable));
		const auto track = dashboard_rect{left, track_top, right, track_top + track_height};
		draw.fill_solid_rect(to_platform_rect(track), GraphBorderColor);
		draw.fill_solid_rect(left, track.top, std::max(0, thumb - left), track.height(), AccentColor);
		const pf::color_t thumb_color = _focus == focus_target::slider ? TextColor : AccentColor;
		draw.fill_ellipse(thumb - scale(_dpi, 6.0), center_y - scale(_dpi, 6.0),
		                  scale(_dpi, 12.0), scale(_dpi, 12.0), thumb_color);
	}

	void draw_edit(pf::draw_context& draw, const dashboard_rect& rect)
	{
		draw.fill_solid_rect(to_platform_rect(rect), SurfaceColor);
		const int border = std::max(1, scale(_dpi, _focus == focus_target::edit ? 2.0 : 1.0));
		const auto border_color = _focus == focus_target::edit ? AccentColor : GraphBorderColor;
		draw.fill_solid_rect(rect.left, rect.top, rect.width(), border, border_color);
		draw.fill_solid_rect(rect.left, rect.bottom - border, rect.width(), border, border_color);
		draw.fill_solid_rect(rect.left, rect.top, border, rect.height(), border_color);
		draw.fill_solid_rect(rect.right - border, rect.top, border, rect.height(), border_color);

		const auto edit_font = font(13);
		const auto extent = draw.measure_text(_edit.edit.text, edit_font);
		const int x = rect.right - scale(_dpi, 5.0) - extent.cx;
		const int y = rect.top + (rect.height() - extent.cy) / 2;
		draw.draw_text(x, y, {x, y, x + std::max(1, extent.cx), y + std::max(1, extent.cy)},
		               _edit.edit.text, edit_font, TextColor, SurfaceColor);

		if (_focus == focus_target::edit)
		{
			_edit.edit.draw_selection(draw, x, y, extent.cy, edit_font);
			if (_edit.caret.visible)
				_edit.edit.draw_caret(draw, x, y, extent.cy, edit_font, _dpi);
		}
	}

	void draw_graph(pf::draw_context& draw, const dashboard_rect& panel)
	{
		const dashboard_rect rect{panel.left + scale(_dpi, 4.0), panel.top + scale(_dpi, 4.0),
		                          panel.right - scale(_dpi, 4.0), panel.bottom - scale(_dpi, 4.0)};
		const int width = rect.width();
		const int height = rect.height();
		const int n_width = std::min(width, usage_data::MaxUses);
		if (width <= 0 || height <= 0 || n_width <= 0)
			return;

		const int smooth_radius = std::clamp(n_width / 80, 3, 12);
		std::vector<float> mouse(n_width), keyboard(n_width), breaks(n_width);
		std::vector<float> temp_mouse(n_width), temp_keyboard(n_width);
		const auto* uses = _usage.get_uses();
		const auto* markers = _usage.get_break_markers();

		for (int i = 0; i < n_width; ++i)
		{
			float sum_mouse = 0.0f;
			float sum_keyboard = 0.0f;
			int count = 0;
			for (int j = std::max(0, i - smooth_radius);
			     j <= std::min(n_width - 1, i + smooth_radius); ++j)
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
			float sum_mouse = 0.0f;
			float sum_keyboard = 0.0f;
			float sum_break = 0.0f;
			int count = 0;
			for (int j = std::max(0, i - smooth_radius);
			     j <= std::min(n_width - 1, i + smooth_radius); ++j)
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

		std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, bitmap_pixel(BackgroundColor));
		const auto grid = bitmap_pixel(blend(BackgroundColor, TextColor, 0.08f));
		for (int g = 1; g <= 4; ++g)
		{
			const int y = height - static_cast<int>(height * g / 4.0f);
			if (y >= 0 && y < height)
				std::fill_n(pixels.data() + static_cast<size_t>(y) * width, width, grid);
		}

		const uint32_t mouse_color = bitmap_pixel(ChartMouseColor);
		const uint32_t keyboard_color = bitmap_pixel(AccentColor);
		const uint32_t break_color = bitmap_pixel(GreenColor);
		const uint32_t outline = bitmap_pixel(blend(AccentColor, TextColor, 0.3f));
		for (int i = 0; i < n_width; ++i)
		{
			const int x = width - 1 - i;
			if (breaks[i] > 0.01f)
			{
				const auto blend_pixel = [base = bitmap_pixel(BackgroundColor)](const uint32_t tint, const float t)
				{
					const auto channel = [t](const uint32_t a, const uint32_t b)
					{
						return static_cast<uint32_t>(a * (1.0f - t) + b * t);
					};
					return (channel((base >> 16) & 0xFF, (tint >> 16) & 0xFF) << 16) |
						(channel((base >> 8) & 0xFF, (tint >> 8) & 0xFF) << 8) |
						channel(base & 0xFF, tint & 0xFF);
				};
				const uint32_t band = blend_pixel(break_color, breaks[i] * 0.25f);
				for (int y = 0; y < height; ++y)
					pixels[static_cast<size_t>(y) * width + x] = band;
			}

			const float mouse_value = mouse[i];
			const float keyboard_value = keyboard[i];
			const float total = mouse_value + keyboard_value;
			if (total <= 0.001f)
				continue;

			const int mouse_height = static_cast<int>(mouse_value * height / max_value);
			const int total_height = static_cast<int>(total * height / max_value);
			for (int dy = 0; dy < mouse_height && dy < height; ++dy)
				pixels[static_cast<size_t>(height - 1 - dy) * width + x] = mouse_color;
			for (int dy = mouse_height; dy < total_height && dy < height; ++dy)
				pixels[static_cast<size_t>(height - 1 - dy) * width + x] = keyboard_color;

			const int top = height - total_height;
			for (int dy = 0; dy < 2; ++dy)
				if (top + dy >= 0 && top + dy < height)
					pixels[static_cast<size_t>(top + dy) * width + x] = outline;
		}

		pf::bitmap image(width, height, std::move(pixels));
		draw.draw_bitmap(rect.left, rect.top, image);

		const auto axis_font = font(9);
		for (const int minutes : {30, 60, 120, 180})
		{
			const int tick = minutes * usage_data::TimerGap;
			if (tick >= n_width)
				continue;
			const int x = rect.right - tick;
			const std::string label = minutes >= 60 && minutes % 60 == 0
				? std::format("{}h", minutes / 60)
				: std::format("{}m", minutes);
			draw_text_box(draw, label, axis_font,
			              {x - scale(_dpi, 12.0), rect.bottom - scale(_dpi, 18.0),
			               x + scale(_dpi, 12.0), rect.bottom},
			              blend(TextColor, BackgroundColor, 0.5f), BackgroundColor,
			              true, false, false);
		}
	}

	void draw_pie(pf::draw_context& draw, const dashboard_rect& panel)
	{
		const int pad = scale(_dpi, 8.0);
		const int size = std::min(panel.width() - pad * 2, panel.height() - pad * 2 - scale(_dpi, 70.0));
		if (size <= scale(_dpi, 20.0))
			return;

		const auto& today = _daily.today();
		const float segments[] = {
			static_cast<float>(today.keyboard_ticks),
			static_cast<float>(today.mouse_ticks),
			static_cast<float>(today.idle_ticks),
			static_cast<float>(today.locked_ticks),
			static_cast<float>(std::max(0, usage_data::MaxUses - today.keyboard_ticks -
			                                  today.mouse_ticks - today.idle_ticks - today.locked_ticks)),
		};
		const pf::color_t colors[] = {AccentColor, ChartMouseColor, GreenColor, YellowColor, PieOffColor};
		const int x = panel.left + (panel.width() - size) / 2;
		const int y = panel.top + pad;
		draw_pie_bitmap(draw, {x, y, x + size, y + size}, segments, colors, 5);

		const auto legend_font = font(9);
		const std::string_view labels[] = {"Keyboard", "Mouse", "Machine On", "Locked", "Off"};
		float total = 0.0f;
		for (const float segment : segments)
			total += segment;
		const int row_height = scale(_dpi, 12.0);
		for (int i = 0; i < 5; ++i)
		{
			const int percent = total > 0.0f
				? static_cast<int>(segments[i] / total * 100.0f + 0.5f)
				: 0;
			const int row_y = y + size + scale(_dpi, 6.0) + i * row_height;
			draw.fill_solid_rect(panel.left + pad, row_y + scale(_dpi, 3.0),
			                    scale(_dpi, 6.0), scale(_dpi, 6.0), colors[i]);
			draw_text_box(draw, std::format("{} {}%", labels[i], percent), legend_font,
			              {panel.left + pad + scale(_dpi, 10.0), row_y,
			               panel.right - pad, row_y + row_height},
			              DimTextColor, SurfaceColor, false, false, true);
		}
	}

	static uint32_t pixel_lerp(const uint32_t a, const uint32_t b, const int t256)
	{
		const int inverse = 256 - t256;
		const uint32_t r = (((a >> 16) & 0xFF) * inverse + ((b >> 16) & 0xFF) * t256) >> 8;
		const uint32_t g = (((a >> 8) & 0xFF) * inverse + ((b >> 8) & 0xFF) * t256) >> 8;
		const uint32_t blue = ((a & 0xFF) * inverse + (b & 0xFF) * t256) >> 8;
		return (r << 16) | (g << 8) | blue;
	}

	void draw_pie_bitmap(pf::draw_context& draw,
	                     const dashboard_rect& rect,
	                     const float* segments,
	                     const pf::color_t* colors,
	                     const int count)
	{
		const int width = rect.width();
		const int height = rect.height();
		const int radius = std::min(width, height) / 2 - 2;
		if (radius < 8)
			return;

		const int cx = width / 2;
		const int cy = height / 2;
		std::array<uint32_t, 64> ring{};
		float total = 0.0f;
		for (int i = 0; i < count; ++i)
			total += segments[i];
		if (total <= 0.0f)
			total = 1.0f;

		int slot = 0;
		for (int i = 0; i < count && slot < 64; ++i)
		{
			int slots = static_cast<int>(std::round(segments[i] / total * 64.0f));
			if (i == count - 1)
				slots = 64 - slot;
			for (int j = 0; j < slots && slot < 64; ++j)
				ring[slot++] = bitmap_pixel(colors[i]);
		}
		if (slot > 0)
			while (slot < 64)
			{
				ring[slot] = ring[slot - 1];
				++slot;
			}

		const uint32_t background = bitmap_pixel(SurfaceColor);
		const uint32_t center = bitmap_pixel(SurfaceColor);
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

		std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, background);
		for (int y = 0; y < height; ++y)
		{
			const int dy = y - cy;
			for (int x = 0; x < width; ++x)
			{
				const int dx = x - cx;
				const int distance = dx * dx + dy * dy;
				uint32_t color = background;
				if (distance < inner_r1)
				{
					color = center;
				}
				else if (distance < outer_r2)
				{
					const double index1 = (std::numbers::pi + std::atan2(static_cast<double>(dy),
					                                             static_cast<double>(dx))) /
					                      std::numbers::pi * 32.0;
					const double index2 = (std::numbers::pi + std::atan2(static_cast<double>(dy + 1),
					                                             static_cast<double>(dx + 1))) /
					                      std::numbers::pi * 32.0;
					const uint32_t c1 = ring[static_cast<int>(index1) % 64];
					const uint32_t c2 = ring[static_cast<int>(index2) % 64];
					color = c1 == c2 ? c1 : pixel_lerp(c1, c2,
						static_cast<int>(std::clamp(std::abs(index2 - index1), 0.0, 1.0) * 255));

					if (distance > outer_r1)
						color = pixel_lerp(color, background,
							static_cast<int>(std::clamp(static_cast<float>(distance - outer_r1) /
							                                outer_diff * 255.0f, 0.0f, 255.0f)));
					else if (distance < inner_r2)
						color = pixel_lerp(center, color,
							static_cast<int>(std::clamp(static_cast<float>(distance - inner_r1) /
							                                inner_diff * 255.0f, 0.0f, 255.0f)));
				}

				const float d = (static_cast<float>(distance) / outer_r2 + dither[x % 4][y % 4]) * 0.25f;
				const int lighten = static_cast<int>(d * 40.0f);
				color = (std::min(255, static_cast<int>((color >> 16) & 0xFF) + lighten) << 16) |
					(std::min(255, static_cast<int>((color >> 8) & 0xFF) + lighten) << 8) |
					std::min(255, static_cast<int>(color & 0xFF) + lighten);
				pixels[static_cast<size_t>(y) * width + x] = color;
			}
		}

		pf::bitmap image(width, height, std::move(pixels));
		draw.draw_bitmap(rect.left, rect.top, image);
	}
};
