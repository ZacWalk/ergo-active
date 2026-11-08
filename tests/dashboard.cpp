// Headless dashboard geometry, rendering, service and policy checks.

#include "libdesktop/platform.h"
#include "libdesktop/ui/test_support.h"

#include "dashboard-layout.h"
#include "dashboard-policy.h"
#include "desktop-services.h"
#include "ui-frame.h"

#include <cstdio>
#include <unordered_map>

namespace
{
int failures = 0;
int checks = 0;
void check(bool condition, const char* description)
{
	++checks;
	if (!condition) ++failures;
	std::printf("  [%s] %s\n", condition ? "pass" : "FAIL", description);
}

void test_layout()
{
	std::printf("DPI-aware control layout\n");
	const auto one = dashboard_layout::measure(880, 460, 1.0);
	const auto two = dashboard_layout::measure(1320, 690, 1.5);
	check(one.slider.width() == 180 && one.edit.width() == 48, "base control widths remain stable");
	check(two.slider.width() == 270 && two.edit.width() == 72, "controls scale with monitor DPI");
	check(one.slider.top >= 0 && one.edit.top >= one.slider.top, "controls stay inside the client layout");
	check(two.content_width > one.content_width, "larger clients keep room for charts");
}

void test_delay_editor()
{
	std::printf("delay slider and editor policy\n");
	delay_control delay(20, 120, 50);
	check(delay.value() == 50 && delay.text() == "50", "initial delay is synchronised");
	delay.text_changed("1");
	check(delay.value() == 20 && delay.text() == "1", "typed values clamp without destroying in-progress text");
	delay.commit();
	check(delay.text() == "20", "leaving the editor commits the clamped value");
	delay.nudge(200);
	check(delay.value() == 120 && delay.text() == "120", "right movement clamps at the maximum");
	delay.nudge(-200);
	check(delay.value() == 20 && delay.text() == "20", "left movement clamps at the minimum");
	delay.text_changed("not a number");
	check(delay.value() == 20, "invalid numeric input is safe and uses the minimum");
}

void test_event_and_reminder_policy()
{
	std::printf("input, timer and reminder policy\n");
	check(classify_input(true, true, true, false).mouse == 1, "cursor movement is mouse activity");
	check(classify_input(true, false, true, false).keyboard == 1, "changed input without cursor movement is keyboard activity");
	check(classify_input(true, true, true, true).mouse == 0, "locked sessions never become active input");
	check(classify_input(true, true, false, false).keyboard == 0, "failed cursor queries are not misclassified");
	check(elapsed_periods(30.0, 10.0, 10.0) == 2, "late timer delivery reports missed periods");
	check(elapsed_periods(10.0, 10.0, 10.0) == 0, "an early timer does not age the model");
	check(choose_reminder(false, 100, 50, true) == reminder_kind::none, "balloon throttling suppresses notifications");
	check(choose_reminder(true, 51, 50, true) == reminder_kind::break_warning, "break warning wins over eye reminder");
	check(choose_reminder(true, 40, 50, true) == reminder_kind::eye_strain, "eye reminder fires before a break is overdue");
	check(urgency_level(0, 50) == 0 && urgency_level(38, 50) == 1 && urgency_level(50, 50) == 2,
	      "tray urgency follows the configured interval");
}

class fake_store final : public activity_store
{
public:
	activity_store_read_result read_text(std::string_view name, std::string& value) const override
	{
		const auto found = text.find(std::string(name));
		if (found == text.end()) return {false, true, {}};
		value = found->second;
		return {};
	}
	activity_store_result write_text_atomic(std::string_view name, std::string_view value) const override
	{
		text[std::string(name)] = std::string(value); ++write_count; return {};
	}
	activity_store_read_result read_binary(std::string_view name, std::vector<uint8_t>& value) const override
	{
		const auto found = binary.find(std::string(name));
		if (found == binary.end()) return {false, true, {}};
		value = found->second; return {};
	}
	activity_store_result write_binary_atomic(std::string_view name, std::span<const uint8_t> value) const override
	{
		binary[std::string(name)] = {value.begin(), value.end()}; ++write_count; return {};
	}
	int64_t seconds_since_written(std::string_view) const override { return 0; }
	int writes() const { return write_count; }
private:
	mutable std::unordered_map<std::string, std::string> text;
	mutable std::unordered_map<std::string, std::vector<uint8_t>> binary;
	mutable int write_count = 0;
};

class fake_input final : public input_monitor
{
public:
	input_sample sample() override { ++samples; return next; }
	input_sample next;
	int samples = 0;
};
class fake_uptime final : public uptime_service
{
public:
	uint64_t milliseconds() override { return now; }
	uint64_t now = 0;
};
class fake_session final : public session_monitor
{
public:
	bool is_locked() const override { return locked; }
	void set_locked(bool value) override { locked = value; }
	bool locked = false;
};
class fake_settings final : public settings_store
{
public:
	int load_delay(int, int, int fallback) const override { return fallback; }
	bool save_delay(int value) const override { delay = value; return true; }
	std::string diagnostics() const override { return {}; }
	mutable int delay = 50;
};
class fake_clipboard final : public clipboard_service
{
public:
	std::string text() const override { return value; }
	bool set_text(std::string_view text) override { value = text; return true; }
	std::string value;
};
class fake_tray final : public tray_service
{
public:
	bool install(std::function<void()> restore, std::function<void(core::pointi, bool)> context_menu,
	             std::function<void()> end_session) override
	{
		this->restore = std::move(restore); this->context_menu = std::move(context_menu);
		this->end_session = std::move(end_session); return true;
	}
	void uninstall() override {}
	void update_urgency(int) override {}
	void update_tooltip(std::string_view) override {}
	void show_balloon(std::string_view, std::string_view, std::string_view) override {}
	std::string diagnostics() const override { return {}; }
	std::function<void()> restore;
	std::function<void(core::pointi, bool)> context_menu;
	std::function<void()> end_session;
};

void test_ui_render_and_edit_model()
{
	std::printf("ui frame rendering and edit model\n");
	fake_store store;
	fake_input input;
	fake_uptime uptime;
	fake_session session;
	fake_settings settings;
	fake_clipboard clipboard;
	fake_tray tray;
	main_frame frame(store, input, uptime, session, settings, clipboard, tray);
	ui::test::fake_measure_context measure;
	frame.on_window_layout(measure, {880, 460}, false);
	ui::test::fake_draw_context draw({0, 0, 880, 460});
	frame.on_window_paint(draw);
	check(draw.drawn_text().find("ergo-active") != std::string::npos &&
	          draw.drawn_text().find("ACTIVE TODAY") != std::string::npos && !draw.fills.empty(),
	      "the UI frame paint retains the dashboard title, cards and surfaces");

	ui::text_edit_model model;
	model.text("50");
	model.select_all();
	model.insert("7");
	check(model.text() == "7" && model.caret() == 1, "the shared edit model replaces a selected numeric value");
	ui::edit_styles styles;
	styles.number = true;
	styles.select_all_on_focus = true;
	auto edit = ui::make_edit(styles, "50", [](const std::string&) {});
	edit->layout(measure, {10, 10, 58, 34});
	check(edit->window_text() == "50", "ui::edit keeps its owner-provided text");
}

void test_sampling_flush_and_session_events()
{
	std::printf("sampling, flush and session events\n");
	fake_store store;
	fake_input input;
	input.next.keyboard = 1;
	fake_uptime uptime;
	fake_session session;
	fake_settings settings;
	fake_clipboard clipboard;
	fake_tray tray;
	main_frame frame(store, input, uptime, session, settings, clipboard, tray);
	uptime.now = 9'999;
	frame.on_timer();
	check(input.samples == 0, "a callback before the ten-second period does not sample");
	uptime.now = 10'000;
	frame.on_timer();
	check(input.samples == 1 && frame.test_keyboard_ticks() == 1,
	      "the first real ten-second period samples keyboard activity");
	session.set_locked(true);
	uptime.now = 20'000;
	frame.on_timer();
	check(frame.test_locked_ticks() == 1, "a locked tick is recorded as time away");
	session.set_locked(false);
	input.next = {};
	uptime.now = 220'000;
	frame.on_timer();
	check(frame.test_break_count() == 1, "suspend-inclusive uptime ages missed periods into a break");
	const int before = store.writes();
	tray.end_session = [&frame] { frame.flush(); };
	tray.end_session();
	check(store.writes() > before, "end-session flushes usage and history through the store");
}

void test_frame_teardown_flushes()
{
	std::printf("frame/service destruction order\n");
	fake_store store;
	fake_input input;
	fake_uptime uptime;
	fake_session session;
	fake_settings settings;
	fake_clipboard clipboard;
	fake_tray tray;
	std::weak_ptr<main_frame> weak;
	{
		auto frame = std::make_shared<main_frame>(store, input, uptime, session, settings, clipboard, tray);
		weak = frame;
	}
	check(weak.expired(), "the dashboard releases when its owner releases it");
	check(store.writes() >= 2 && settings.delay == 50,
	      "dashboard teardown flushes through still-live fake services");
}

void test_shared_controls_and_policy_state()
{
	std::printf("shared controls and close policy\n");
	fake_store store;
	fake_input input;
	fake_uptime uptime;
	fake_session session;
	fake_settings settings;
	fake_clipboard clipboard;
	fake_tray tray;
	main_frame frame(store, input, uptime, session, settings, clipboard, tray);
	frame.test_editor_changed("30 ");
	check(frame.test_editor_text() == "30" && frame.test_delay_value() == 30,
	      "pasting whitespace into the delay box keeps only digits");
	frame.test_mark_session_ending();
	check(frame.test_take_skip_close_prompt(), "session ending bypasses the user-close prompt");
	main_frame explicit_exit(store, input, uptime, session, settings, clipboard, tray);
	explicit_exit.request_close(true);
	check(explicit_exit.test_take_skip_close_prompt(), "explicit Exit bypasses the user-close prompt");

	ui::test::fake_control_host host;
	auto input_manager = std::make_shared<ui::input_manager>(&host);
	ui::edit_styles edit_styles;
	edit_styles.number = true;
	edit_styles.select_all_on_focus = true;
	std::string edit_value;
	auto edit = ui::make_edit(edit_styles, "50", [&](const std::string& value) { edit_value = value; });
	edit->layout(host.measure_context, {0, 0, 80, 24});
	edit->found(input_manager, {}, true);
	edit->on_focus(true, true);
	edit->on_mouse_down({8, 8}, {});
	edit->on_text("9");
	check(edit_value.size() > 1 && edit_value.find('9') != std::string::npos,
	      "clicking the shared edit places the caret instead of replacing all text");
	edit_value.clear();
	edit->on_text("7x");
	check(edit_value.find('x') == std::string::npos, "the shared numeric edit rejects non-digit text");

	std::vector<bool> slider_tracking;
	auto slider = ui::make_slider(20, 120, [&](int, bool tracking) { slider_tracking.push_back(tracking); });
	slider->layout(host.measure_context, {0, 0, 180, 28});
	slider->found(input_manager, {}, true);
	slider->on_mouse_down({20, 14}, {});
	slider->on_mouse_move({160, 14}, true);
	slider->on_mouse_up({160, 14}, {});
	check(slider_tracking.size() >= 2 && slider_tracking.front() && !slider_tracking.back(),
	      "slider drag reports capture-time and release changes through ui::trackbar");
}

void test_shared_publication_failures()
{
	std::printf("shared publication failure seams\n");
	platform::test_set_tray_publication_failure(true);
	desktop_tray tray;
	const bool installed = tray.install([] {}, [](core::pointi, bool) {}, [] {});
	check(!installed && !tray.diagnostics().empty(), "tray publication failure reaches the app seam");
	tray.uninstall();
	platform::test_set_tray_publication_failure(false);

	platform::test_set_atomic_file_failure(platform::atomic_file_test_failure::open);
	const auto result = platform::create_atomic_file_writer_result(core::file_path("ergo-active-test-no-write.dat"));
	check(!result.success() && !result.result.error_message.empty(), "atomic-file failure returns a diagnostic");
	platform::test_set_atomic_file_failure(platform::atomic_file_test_failure::none);
}
}

int main()
{
	test_layout();
	test_delay_editor();
	test_event_and_reminder_policy();
	test_ui_render_and_edit_model();
	test_sampling_flush_and_session_events();
	test_frame_teardown_flushes();
	test_shared_controls_and_policy_state();
	test_shared_publication_failures();
	std::printf("\n%s (%d checks)\n", failures == 0 ? "all checks passed" : "there were failures", checks);
	return failures == 0 ? 0 : 1;
}
