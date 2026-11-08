// app-services.h - Neutral seams between the dashboard and Windows services.
//
// The dashboard talks to these small contracts. Their Win32 monitoring
// implementation lives in win-services.h; generic files, settings, clipboard
// and tray behavior belongs to libdesktop's platform layer.

#pragma once

#include "libdesktop/core.h"

#include <cstdint>
#include <functional>
#include <string_view>

struct input_sample
{
	int64_t mouse = 0;
	int64_t keyboard = 0;
};

struct input_monitor
{
	virtual ~input_monitor() = default;
	virtual input_sample sample() = 0;
};

struct uptime_service
{
	virtual ~uptime_service() = default;
	virtual uint64_t milliseconds() = 0;
};

struct session_monitor
{
	virtual ~session_monitor() = default;
	virtual void poll() {}
	virtual bool is_locked() const = 0;
	virtual void set_locked(bool) {}
};

struct settings_store
{
	virtual ~settings_store() = default;
	virtual int load_delay(int minimum, int maximum, int fallback) const = 0;
	virtual bool save_delay(int delay) const = 0;
	virtual std::string diagnostics() const = 0;
};

struct clipboard_service
{
	virtual ~clipboard_service() = default;
	virtual std::string text() const = 0;
	virtual bool set_text(std::string_view text) = 0;
};

struct tray_service
{
	virtual ~tray_service() = default;

	virtual bool install(std::function<void()> restore,
	                     std::function<void(core::pointi, bool)> context_menu,
	                     std::function<void()> end_session) = 0;
	virtual void uninstall() = 0;
	virtual void update_urgency(int level) = 0;
	virtual void update_tooltip(std::string_view text) = 0;
	virtual void show_balloon(std::string_view title,
	                          std::string_view message,
	                          std::string_view tip) = 0;
	virtual std::string diagnostics() const = 0;
};
