// main.cpp - ergo-active application callbacks for libdesktop-core.

#include "platform.h"

#include "ui-frame.h"
#include "desktop-services.h"
#include "win-services.h"

#include <memory>

namespace
{
	constexpr std::string_view InstanceMutexName = "Local\\ergo-active-single-instance";
	constexpr std::string_view ShowWindowSignal = "ergo-active-show-window";
	pf::instance_lock_ptr instance_lock;
	pf::window_frame_ptr app_window;
	std::unique_ptr<desktop_activity_store> activity_store_backend;
	std::unique_ptr<win_input_monitor> input_backend;
	std::unique_ptr<desktop_uptime> uptime_backend;
	std::unique_ptr<win_session_state> session_backend;
	std::unique_ptr<desktop_settings> settings_backend;
	std::unique_ptr<desktop_clipboard> clipboard_backend;
	std::unique_ptr<desktop_tray> tray_backend;
	std::shared_ptr<main_frame> dashboard;
}

const std::string_view s_app_name = "ergo-active";

app_init_result app_init(const pf::window_frame_ptr& frame,
                         std::span<const std::string_view>)
{
	auto locked = pf::try_lock_named_instance(InstanceMutexName);
	if (locked.already_running)
	{
		pf::signal_existing_instance(ShowWindowSignal);
		return {.start_gui = false, .exit_code = 0};
	}

	if (!locked.lock)
	{
		pf::debug_trace("ergo-active could not acquire its instance lock: " + locked.error + "\n");
		return {.start_gui = false, .exit_code = 1};
	}

	instance_lock = std::move(locked.lock);
	activity_store_backend = std::make_unique<desktop_activity_store>();
	if (!activity_store_backend->ready())
		pf::debug_trace("ergo-active activity persistence unavailable: " +
		                activity_store_backend->diagnostics() + "\n");
	input_backend = std::make_unique<win_input_monitor>();
	uptime_backend = std::make_unique<desktop_uptime>();
	session_backend = std::make_unique<win_session_state>();
	settings_backend = std::make_unique<desktop_settings>();
	clipboard_backend = std::make_unique<desktop_clipboard>();
	tray_backend = std::make_unique<desktop_tray>();
	dashboard = std::make_shared<main_frame>(*activity_store_backend,
	                                         *input_backend,
	                                         *uptime_backend,
	                                         *session_backend,
	                                         *settings_backend,
	                                         *clipboard_backend,
	                                         *tray_backend);

	frame->set_reactor(dashboard);
	app_window = frame;
	const std::weak_ptr<main_frame> weak_dashboard = dashboard;
	pf::register_instance_signal(ShowWindowSignal, [weak_dashboard]
	{
		if (const auto current = weak_dashboard.lock())
			current->restore();
	});
	dashboard->install_menu(frame);

	app_init_result result;
	result.window.initial_client_size = {880, 460};
	result.window.minimum_client_size = {700, 420};
	result.window.resizable = true;
	return result;
}

void app_idle()
{
}

void app_destroy()
{
	// The platform keeps the reactor on its frame until the owner clears it.
	// Detach before destroying the dashboard or any service it borrows.
	if (app_window)
		app_window->set_reactor({});
	dashboard.reset();
	tray_backend.reset();
	settings_backend.reset();
	clipboard_backend.reset();
	uptime_backend.reset();
	session_backend.reset();
	input_backend.reset();
	activity_store_backend.reset();
	app_window.reset();
	instance_lock.reset();
}
