// main.cpp - ergo-active application lifecycle for libdesktop's four libraries.

#include "libdesktop/platform.h"
#include "libdesktop/ui.h"

#include "ui-frame.h"
#include "desktop-services.h"
#include "win-services.h"

#include <memory>

namespace
{
constexpr std::string_view InstanceMutexName = "Local\\ergo-active-single-instance";
constexpr std::string_view ShowWindowSignal = "ergo-active-show-window";

class ergo_app final : public ui::app, public std::enable_shared_from_this<ergo_app>
{
public:
	bool load_settings(const platform::setting_file_ptr& store) override
	{
		_settings = store;
		return true;
	}

	bool init(std::string_view) override
	{
		_session_ending = false;
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

		const bool saved_placement = platform::read_placement(
			_settings, platform::placement_setting_name()).has_value();
		ui::frame_style style;
		style.drawn_caption = false;
		style.can_focus = true;
		style.client_size = {880, 460};
		style.minimum_client = {700, 420};
		style.resizable = true;
		style.hardware_accelerated = false;
		style.colors = {core::rgba(0x1A, 0x1A, 0x2E), core::rgba(0xE0, 0xE0, 0xE0), core::rgba(0x3A, 0x9B, 0xDC)};
		_app_window = ui::create_app_frame(shared_from_this(), _settings, _dashboard, style);
		if (!_app_window)
		{
			core::log("ergo-active", "could not create the dashboard window");
			platform::quit(1);
			return false;
		}
		if (!saved_placement)
		{
			const auto current = _app_window->window_bounds();
			const auto screen = platform::primary_screen_size();
			if (!current.extent().is_empty() && !screen.is_empty())
			{
				const auto centered = core::recti{
					(screen.cx - current.width()) / 2, (screen.cy - current.height()) / 2,
					(screen.cx + current.width()) / 2, (screen.cy + current.height()) / 2};
				_app_window->window_bounds(centered, false);
			}
		}
		_dashboard->attach(_app_window);
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
		if (_session_ending || (_dashboard && _dashboard->take_skip_close_prompt()))
			return true;
		const auto result = platform::message_box(nullptr,
			"When hidden, ergo-active will continue monitoring activity and remind you to take breaks.\n\n"
			"Choose Yes to hide, No to exit, or Cancel to keep the dashboard open.",
			"ergo-active", {platform::message_box_buttons::yes_no_cancel, platform::message_box_icon::question});
		if (result == platform::message_box_result::yes)
		{
			if (_dashboard) _dashboard->hide();
			return false;
		}
		return result == platform::message_box_result::no;
	}

	void exit() override
	{
		if (_dashboard) _dashboard->flush();
	}

	void final_exit() override
	{
		platform::set_frame_delay(0);
		_session_ending = false;
		_app_window.reset();
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

	bool wants_gpu() const override { return false; }
	bool text_input(std::string_view) override { return false; }
	ui::focus_mode focus_mode() const override
	{
		return _dashboard && _dashboard->editor_focused() ? ui::focus_mode::text_edit : ui::focus_mode::none;
	}
	bool key_down(char32_t key, platform::key_state keys) override
	{
		return _dashboard && _dashboard->dispatch_key(key, keys);
	}
	void track_menu(const ui::frame_ptr& parent, core::recti bounds,
	               const std::vector<ui::command_ptr>& commands) override
	{
		if (parent) parent->track_menu(bounds, commands);
	}

private:
	platform::setting_file_ptr _settings;
	platform::instance_lock_ptr _instance_lock;
	std::unique_ptr<desktop_activity_store> _activity_store;
	std::unique_ptr<win_input_monitor> _input;
	std::unique_ptr<desktop_uptime> _uptime;
	std::unique_ptr<win_session_state> _session;
	std::unique_ptr<desktop_settings> _settings_backend;
	std::unique_ptr<desktop_clipboard> _clipboard;
	std::unique_ptr<desktop_tray> _tray;
	std::shared_ptr<main_frame> _dashboard;
	ui::frame_ptr _app_window;
	bool _session_ending = false;
};
}

int main(int argc, char* argv[])
{
	core::set_app_name("ergo-active");
	auto app = std::make_shared<ergo_app>();
	return platform::run(app, argc, argv);
}
