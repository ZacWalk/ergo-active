// main.cpp - ergo-active's application: a libdesktop ui::shell around the dashboard.

#include "libdesktop/platform.h"
#include "libdesktop/ui.h"
#include "libdesktop/ui/shell.h"

#include "ui-frame.h"
#include "desktop-services.h"
#include "win-services.h"

#include <memory>

namespace
{
	constexpr std::string_view InstanceMutexName = "Local\\ergo-active-single-instance";
	constexpr std::string_view ShowWindowSignal = "ergo-active-show-window";

	class ergo_app final : public ui::shell
	{
	public:
		ergo_app() : shell(make_options()) {}

		bool init(const std::string_view command_line) override
		{
			_session_ending = false;
			// A second copy signals the first and exits before the shell makes a window.
			auto locked = platform::try_lock_named_instance(InstanceMutexName);
			if (locked.already_running)
			{
				platform::signal_existing_instance(ShowWindowSignal);
				return false;
			}
			if (!locked.lock)
			{
				core::log("ergo-active", "could not acquire its instance lock: " + locked.error);
				platform::quit(1);
				return false;
			}
			_instance_lock = std::move(locked.lock);

			_activity_store = std::make_unique<desktop_activity_store>();
			if (!_activity_store->ready())
				core::log("ergo-active", "activity persistence unavailable: " + _activity_store->diagnostics());
			_input = std::make_unique<win_input_monitor>();
			_uptime = std::make_unique<desktop_uptime>();
			_session = std::make_unique<win_session_state>();
			_settings_backend = std::make_unique<desktop_settings>();
			_clipboard = std::make_unique<desktop_clipboard>();
			_tray = std::make_unique<desktop_tray>();
			_dashboard = std::make_shared<main_frame>(*_activity_store, *_input, *_uptime, *_session,
			                                          *_settings_backend, *_clipboard, *_tray);
			if (shell::init(command_line)) return true;
			core::log("ergo-active", "could not create the dashboard window");
			platform::quit(1);
			return false;
		}

		std::vector<ui::command_ptr> make_menus() override { return _dashboard->menus(); }
		ui::element_ptr make_content() override { return _dashboard->content(); }

		bool start(std::string_view) override
		{
			_dashboard->attach(frame());
			platform::set_frame_delay(static_cast<int>(main_frame::TimerPeriodSeconds * 1000.0));
			const std::weak_ptr<main_frame> weak_dashboard = _dashboard;
			platform::register_instance_signal(ShowWindowSignal, [weak_dashboard]
			{
				if (const auto current = weak_dashboard.lock()) current->restore();
			});
			return true;
		}

		void frame_tick() override
		{
			if (_dashboard) _dashboard->on_timer();
		}

		void system_event(const platform::os_event_type event) override
		{
			if (event == platform::os_event_type::session_ending)
			{
				_session_ending = true;
				if (_dashboard) _dashboard->mark_session_ending();
			}
		}

		bool can_exit() override
		{
			if (_session_ending || (_dashboard && _dashboard->take_skip_close_prompt())) return shell::can_exit();
			const auto result = platform::message_box(
				nullptr,
				"When hidden, ergo-active will continue monitoring activity and remind you to take breaks.\n\n"
				"Choose Yes to hide, No to exit, or Cancel to keep the dashboard open.",
				"ergo-active", {platform::message_box_buttons::yes_no_cancel, platform::message_box_icon::question});
			if (result == platform::message_box_result::yes)
			{
				if (_dashboard) _dashboard->hide();
				return false;
			}
			return result == platform::message_box_result::no && shell::can_exit();
		}

		void stop() override
		{
			if (_dashboard) _dashboard->flush();
		}

		void final_exit() override
		{
			platform::set_frame_delay(0);
			_session_ending = false;
			shell::final_exit();
			_dashboard.reset();
			_tray.reset();
			_clipboard.reset();
			_settings_backend.reset();
			_session.reset();
			_uptime.reset();
			_input.reset();
			_activity_store.reset();
			_instance_lock.reset();
		}

		ui::focus_mode focus_mode() const override
		{
			return _dashboard && _dashboard->editor_focused() ? ui::focus_mode::text_edit : ui::focus_mode::none;
		}

		void focus_changed(const bool has_focus, const ui::control_ptr& child) override
		{
			shell::focus_changed(has_focus, child);
			if (_dashboard) _dashboard->focus_changed(has_focus, child);
		}

	private:
		static ui::shell_options make_options()
		{
			ui::shell_options options;
			options.client_size = {880, 460};
			options.minimum_client = {700, 420};
			options.colors = render::color_style{core::rgba(0x1A, 0x1A, 0x2E), core::rgba(0xE0, 0xE0, 0xE0),
			                                     core::rgba(0x3A, 0x9B, 0xDC)};
			return options;
		}

		platform::instance_lock_ptr _instance_lock;
		std::unique_ptr<desktop_activity_store> _activity_store;
		std::unique_ptr<win_input_monitor> _input;
		std::unique_ptr<desktop_uptime> _uptime;
		std::unique_ptr<win_session_state> _session;
		std::unique_ptr<desktop_settings> _settings_backend;
		std::unique_ptr<desktop_clipboard> _clipboard;
		std::unique_ptr<desktop_tray> _tray;
		std::shared_ptr<main_frame> _dashboard;
		bool _session_ending = false;
	};
}

int main(int argc, char* argv[])
{
	core::set_app_name("ergo-active");
	auto app = std::make_shared<ergo_app>();
	return platform::run(app, argc, argv);
}
