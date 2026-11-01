// Headless dashboard geometry, input and notification policy checks.

#include "dashboard-layout.h"
#include "dashboard-policy.h"
#include "ui-frame.h"
#include "ui/ui.h"
#include "ui/test_support.h"
#include "desktop-services.h"

#include <cstdio>
#include <unordered_map>

namespace
{
	int failures = 0;
	int checks = 0;

	void check(const bool condition, const char* description)
	{
		++checks;
		if (!condition)
			++failures;
		std::printf("  [%s] %s\n", condition ? "pass" : "FAIL", description);
	}

	void test_layout()
	{
		std::printf("DPI-aware control layout\n");
		const auto one = dashboard_layout::measure(880, 460, 1.0);
		const auto two = dashboard_layout::measure(1320, 690, 1.5);
		check(one.slider.width() == 180 && one.edit.width() == 48,
		      "the base slider and numeric editor retain their designed widths");
		check(two.slider.width() == 270 && two.edit.width() == 72,
		      "controls scale with the monitor DPI");
		check(one.slider.top >= 0 && one.edit.top >= one.slider.top,
		      "controls stay inside the client layout");
		check(two.content_width > one.content_width,
		      "the dashboard keeps extra space for charts at a larger client size");
	}

	void test_delay_editor()
	{
		std::printf("delay slider and editor policy\n");
		delay_control delay(20, 120, 50);
		check(delay.value() == 50 && delay.text() == "50", "initial delay is synchronised");
		delay.text_changed("1");
		check(delay.value() == 20 && delay.text() == "1",
		      "typed values clamp the model without destroying in-progress text");
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
		check(classify_input(true, true, true, false).mouse == 1,
		      "cursor movement is classified as mouse activity");
		check(classify_input(true, false, true, false).keyboard == 1,
		      "a changed input tick without cursor movement is keyboard activity");
		check(classify_input(true, true, true, true).mouse == 0,
		      "locked sessions never become active input");
		check(classify_input(true, true, false, false).keyboard == 0,
		      "a failed cursor query is not misclassified");
		check(elapsed_periods(30.0, 10.0, 10.0) == 2,
		      "late timer delivery reports the missed whole periods");
		check(elapsed_periods(10.0, 10.0, 10.0) == 0,
		      "a timer with no elapsed period does not age the model");
		check(choose_reminder(false, 100, 50, true) == reminder_kind::none,
		      "balloon throttling suppresses all notifications");
		check(choose_reminder(true, 51, 50, true) == reminder_kind::break_warning,
		      "an overdue break warning wins over the eye reminder");
		check(choose_reminder(true, 40, 50, true) == reminder_kind::eye_strain,
		      "the eye reminder fires when the break is not overdue");
		check(urgency_level(0, 50) == 0 && urgency_level(38, 50) == 1 &&
		          urgency_level(50, 50) == 2,
		      "tray urgency follows the configured break interval");
	}

	class fake_store final : public activity_store
	{
	public:
		activity_store_read_result read_text(std::string_view name,
		                                     std::string& value) const override
		{
			const auto found = text.find(std::string(name));
			if (found == text.end())
				return {false, true, {}};
			value = found->second;
			return {};
		}

		activity_store_result write_text_atomic(std::string_view name,
		                                        std::string_view value) const override
		{
			text[std::string(name)] = std::string(value);
			++write_count;
			return {};
		}

		activity_store_read_result read_binary(std::string_view name,
		                                       std::vector<uint8_t>& value) const override
		{
			const auto found = binary.find(std::string(name));
			if (found == binary.end())
				return {false, true, {}};
			value = found->second;
			return {};
		}

		activity_store_result write_binary_atomic(std::string_view name,
		                                          std::span<const uint8_t> value) const override
		{
			binary[std::string(name)] = {value.begin(), value.end()};
			++write_count;
			return {};
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
		input_sample sample() override
		{
			++samples;
			return next;
		}

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
		void set_locked(const bool value) override { locked = value; }
		bool locked = false;
	};

	class fake_settings final : public settings_store
	{
	public:
		int load_delay(int, int, int fallback) const override { return fallback; }
		bool save_delay(int value) const override
		{
			delay = value;
			return true;
		}
		std::string diagnostics() const override { return {}; }
		mutable int delay = 50;
	};

	class fake_clipboard final : public clipboard_service
	{
	public:
		std::string text() const override { return value; }
		bool set_text(const std::string_view text) override
		{
			value = text;
			return true;
		}
		std::string value;
	};

	class fake_tray final : public tray_service
	{
	public:
		bool install(std::function<void()> restore,
		             std::function<void(pf::ipoint, bool)> context_menu,
		             std::function<void()> end_session) override
		{
			this->restore = std::move(restore);
			this->context_menu = std::move(context_menu);
			this->end_session = std::move(end_session);
			return true;
		}
		void uninstall() override {}
		void update_urgency(int) override {}
		void update_tooltip(std::string_view) override {}
		void show_balloon(std::string_view, std::string_view, std::string_view) override {}
		std::string diagnostics() const override { return {}; }

		std::function<void()> restore;
		std::function<void(pf::ipoint, bool)> context_menu;
		std::function<void()> end_session;
	};

	void test_actual_edit_reactor()
	{
		std::printf("reactor-backed edit control\n");
		fake_store store;
		fake_input input;
		fake_uptime uptime;
		fake_session session;
		fake_settings settings;
		fake_clipboard clipboard;
		fake_tray tray;
		main_frame frame(store, input, uptime, session, settings, clipboard, tray);
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr base = window;
		pf::message_params params;
		frame.handle_message(base, pf::message_type::create, params);
		pf::ui::test::fake_measure_context measure;
		frame.handle_size(base, {880, 460}, measure);
		pf::ui::test::fake_draw_context draw({0, 0, 880, 460});
		pf::window_frame_ptr paint_window = base;
		frame.handle_paint(paint_window, draw);
		check(draw.drawn_text().find("ergo-active") != std::string::npos &&
		          draw.drawn_text().find("ACTIVE TODAY") != std::string::npos &&
		          !draw.fills.empty(),
		      "the production reactor paint retains the dashboard title, cards and surfaces");

		frame.handle_keyboard(base, pf::keyboard_message_type::key_down,
		                     {.vk = pf::platform_key::Tab});
		frame.handle_keyboard(base, pf::keyboard_message_type::key_down,
		                     {.vk = pf::platform_key::Tab});
		check(frame.test_editor_has_selection(), "Tab focuses the shared edit widget with selection");

		frame.handle_keyboard(base, pf::keyboard_message_type::char_input,
		                     {.ch = U'7'});
		check(frame.test_editor_text() == "7" && frame.test_delay_value() == 20,
		      "the shared edit widget preserves in-progress text while the model clamps");

		window->held_keys.insert(pf::platform_key::Control);
		frame.handle_keyboard(base, pf::keyboard_message_type::key_down,
		                     {.vk = 'A'});
		window->held_keys.erase(pf::platform_key::Control);
		frame.handle_keyboard(base, pf::keyboard_message_type::char_input,
		                     {.ch = U'8'});
		check(frame.test_editor_text() == "8",
		      "Ctrl+A selection routes through the reactor before numeric input");

		window->held_keys.insert(pf::platform_key::Control);
		frame.handle_keyboard(base, pf::keyboard_message_type::key_down,
		                     {.vk = 'A'});
		frame.handle_keyboard(base, pf::keyboard_message_type::key_down,
		                     {.vk = 'C'});
		check(clipboard.value == "8", "Ctrl+C uses the shared clipboard seam");
		frame.handle_keyboard(base, pf::keyboard_message_type::key_down,
		                     {.vk = 'X'});
		check(frame.test_editor_text().empty(), "Ctrl+X deletes through the shared edit model");
		frame.handle_keyboard(base, pf::keyboard_message_type::key_down,
		                     {.vk = 'V'});
		window->held_keys.erase(pf::platform_key::Control);
		check(frame.test_editor_text() == "8", "Ctrl+V filters and inserts shared clipboard text");

		const auto layout = dashboard_layout::measure(880, 460, 1.0);
		frame.handle_mouse(base, pf::mouse_message_type::left_button_down,
		                  {.point = {layout.edit.left, layout.edit.top}});
		frame.handle_keyboard(base, pf::keyboard_message_type::char_input,
		                     {.ch = U'9'});
		check(frame.test_editor_text() == "98",
		      "clicking the shared edit widget places the caret instead of replacing all text");

		const auto slider_x = (layout.slider.left + layout.slider.right) / 2;
		const auto slider_y = (layout.slider.top + layout.slider.bottom) / 2;
		frame.handle_mouse(base, pf::mouse_message_type::left_button_down,
		                  {.point = {slider_x, slider_y}});
		check(window->captured && frame.test_slider_dragging(),
		      "slider mouse-down captures the shared frame");
		frame.handle_mouse(base, pf::mouse_message_type::mouse_move,
		                  {.point = {layout.slider.right + 100, slider_y}});
		frame.handle_mouse(base, pf::mouse_message_type::left_button_up,
		                  {.point = {layout.slider.right + 100, slider_y}});
		check(!window->captured && !frame.test_slider_dragging(),
		      "slider release outside its bounds releases capture");
	}

	void test_frame_destruction_flushes_while_services_live()
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
			auto frame = std::make_shared<main_frame>(
				store, input, uptime, session, settings, clipboard, tray);
			weak = frame;
		}
		check(weak.expired(), "the dashboard releases when its owner releases it");
		check(store.writes() >= 2 && settings.delay == 50,
		      "dashboard teardown flushes through still-live fake services");
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
		auto window = std::make_shared<pf::ui::test::fake_window_frame>();
		pf::window_frame_ptr base = window;
		pf::message_params create;
		frame.handle_message(base, pf::message_type::create, create);

		pf::message_params timer;
		timer.timer_id = main_frame::TimerId;
		frame.handle_message(base, pf::message_type::timer, timer);
		check(input.samples == 1 && frame.test_keyboard_ticks() == 1,
		      "the production timer reactor samples input and records keyboard activity");

		session.set_locked(true);
		frame.handle_message(base, pf::message_type::timer, timer);
		check(frame.test_locked_ticks() == 1,
		      "the tray/session callback makes a locked tick time away");

		session.set_locked(false);
		input.next = {};
		uptime.now = 200'000;
		frame.handle_message(base, pf::message_type::timer, timer);
		check(frame.test_break_count() == 1,
		      "suspend-inclusive uptime ages missed timer periods into a break");

		const int before = store.writes();
		tray.end_session();
		check(store.writes() > before,
		      "end-session callback flushes usage and history through the fake store");
	}

	void test_shared_tray_publication_failure()
	{
		std::printf("shared tray publication failure\n");
		pf::test_set_tray_publication_failure(true);
		desktop_tray tray;
		const bool installed = tray.install([] {}, [](pf::ipoint, bool) {}, [] {});
		check(!installed && !tray.diagnostics().empty(),
		      "a shared tray publication failure reaches the app policy seam");
		tray.uninstall();
		pf::test_set_tray_publication_failure(false);
	}

	void test_shared_atomic_publication_failure()
	{
		std::printf("shared atomic publication failure\n");
		pf::test_set_atomic_file_failure(pf::atomic_file_test_failure::open);
		const auto result = pf::create_atomic_file_writer_result(
			pf::file_path("ergo-active-test-no-write.dat"));
		check(!result.success() && !result.result.message.empty(),
		      "a shared atomic-file open failure returns a diagnostic without touching user data");
		pf::test_set_atomic_file_failure(pf::atomic_file_test_failure::none);
	}
}

app_init_result app_init(const pf::window_frame_ptr&, std::span<const std::string_view>)
{
	return {.start_gui = false, .exit_code = 0};
}

void app_idle()
{
}

void app_destroy()
{
}

int main()
{
	test_layout();
	test_delay_editor();
	test_event_and_reminder_policy();
	test_actual_edit_reactor();
	test_frame_destruction_flushes_while_services_live();
	test_sampling_flush_and_session_events();
	test_shared_tray_publication_failure();
	test_shared_atomic_publication_failure();
	std::printf("\n%s (%d checks)\n",
	            failures == 0 ? "all checks passed" : "there were failures", checks);
	return failures == 0 ? 0 : 1;
}
