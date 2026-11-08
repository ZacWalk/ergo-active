// desktop-services.h - Thin ergo-active adapters over libdesktop services.
//
// The platform library owns the filesystem, settings, clipboard and tray
// mechanisms; this file supplies the app-specific paths and policy callbacks.

#pragma once

#include "libdesktop/platform.h"

#include "app-services.h"
#include "data.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class desktop_activity_store final : public activity_store
{
public:
	desktop_activity_store()
	{
		const auto app_data = platform::known_path(platform::known_folder::roaming_app_data);
		if (app_data.is_empty())
		{
			_diagnostic = "The roaming application-data folder is unavailable.";
			return;
		}

		_root = app_data.combine("ergo-active");
		const auto created = platform::create_folder(_root);
		if (!created.success() && !platform::is_directory(_root))
		{
			_diagnostic = "Could not create " + std::string(_root.text());
			if (!created.error_message.empty())
				_diagnostic += ": " + created.error_message;
			return;
		}
		_ready = true;
	}

	activity_store_read_result read_text(const std::string_view name,
	                                     std::string& value) const override
	{
		std::vector<uint8_t> bytes;
		const auto result = read_bytes(name, bytes);
		if (!result)
			return result;
		value.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		return result;
	}

	activity_store_result write_text_atomic(const std::string_view name,
	                                        const std::string_view value) const override
	{
		return write_bytes(name, {reinterpret_cast<const uint8_t*>(value.data()), value.size()});
	}

	activity_store_read_result read_binary(const std::string_view name,
	                                       std::vector<uint8_t>& value) const override
	{
		return read_bytes(name, value);
	}

	activity_store_result write_binary_atomic(const std::string_view name,
	                                          const std::span<const uint8_t> value) const override
	{
		return write_bytes(name, value);
	}

	int64_t seconds_since_written(const std::string_view name) const override
	{
		const auto path = file_path(name);
		const auto age = platform::file_age_seconds(path);
		if (!age)
			return 0;
		return std::max<int64_t>(0, static_cast<int64_t>(*age));
	}

	[[nodiscard]] bool ready() const { return _ready; }
	[[nodiscard]] const std::string& diagnostics() const { return _diagnostic; }

private:
	core::folder_path _root;
	bool _ready = false;
	std::string _diagnostic;

	core::file_path file_path(const std::string_view name) const
	{
		if (!_ready)
			return {};
		if (name == "history.csv" || name == "usage.dat")
			return _root.combine_file(name);
		return {};
	}

	static activity_store_result failure(std::string message)
	{
		return {false, std::move(message)};
	}

	activity_store_read_result read_bytes(const std::string_view name,
	                                      std::vector<uint8_t>& value) const
	{
		const auto path = file_path(name);
		if (path.is_empty())
			return {false, false, "The activity-data folder is unavailable."};

		const auto input = platform::open_file(path, platform::file_open_mode::read);
		if (!input)
		{
			if (platform::file_attributes(path).confirmed_missing())
				return {false, true, {}};
			return {false, false, "Could not read " + path.str()};
		}

		const auto size = input->size();
		if (size > std::numeric_limits<size_t>::max())
			return {false, false, "Activity-data file is too large."};
		value.resize(static_cast<size_t>(size));
		size_t offset = 0;
		while (offset < value.size())
		{
			const auto read = input->read(value.data() + offset, value.size() - offset);
			if (read == 0)
			{
				value.clear();
				return {false, false, "Could not read the complete activity-data file."};
			}
			offset += static_cast<size_t>(read);
		}
		return {};
	}

	activity_store_result write_bytes(const std::string_view name,
	                                  const std::span<const uint8_t> value) const
	{
		const auto destination = file_path(name);
		if (destination.is_empty())
			return failure(_diagnostic.empty() ? "The activity-data folder is unavailable." : _diagnostic);

		const auto opened = platform::create_atomic_file_writer_result(destination);
		if (!opened.success())
			return failure(opened.result.error_message.empty()
			                  ? "Could not stage " + destination.str()
			                  : opened.result.error_message);

		if (const auto result = opened.writer->write(value); !result.success())
			return failure(result.error_message.empty() ? "Could not write activity data." : result.error_message);
		if (const auto result = opened.writer->flush(); !result.success())
			return failure(result.error_message.empty() ? "Could not flush activity data." : result.error_message);
		if (const auto result = opened.writer->commit(); !result.success())
			return failure(result.error_message.empty() ? "Could not publish activity data." : result.error_message);
		return {};
	}
};

class desktop_settings final : public settings_store
{
public:
	desktop_settings()
		: _settings(platform::create_registry_settings("Software\\ergo-active"))
	{
	}

	int load_delay(const int minimum, const int maximum, const int fallback) const override
	{
		uint32_t value = 0;
		if (_settings && _settings->read({}, "Delay", value))
			return std::clamp(static_cast<int>(value), minimum, maximum);
		return fallback;
	}

	bool save_delay(const int delay) const override
	{
		if (_settings && _settings->write({}, "Delay", static_cast<uint32_t>(delay)))
		{
			_diagnostic.clear();
			return true;
		}

		_diagnostic = "Could not save HKCU\\Software\\ergo-active\\Delay.";
		return false;
	}

	std::string diagnostics() const override
	{
		return _diagnostic;
	}

private:
	platform::setting_file_ptr _settings;
	mutable std::string _diagnostic;
};

class desktop_clipboard final : public clipboard_service
{
public:
	std::string text() const override
	{
		if (const auto value = platform::clipboard_text())
			return *value;
		return {};
	}

	bool set_text(const std::string_view value) override
	{
		return platform::set_clipboard(value);
	}
};

class desktop_uptime final : public uptime_service
{
public:
	uint64_t milliseconds() override
	{
		return static_cast<uint64_t>(std::max(0.0, platform::monotonic_seconds()) * 1000.0);
	}
};

class desktop_tray final : public tray_service
{
public:
	bool install(std::function<void()> restore,
	             std::function<void(core::pointi, bool)> context_menu,
	             std::function<void()> end_session) override
	{
		_restore = std::move(restore);
		_context_menu = std::move(context_menu);
		_end_session = std::move(end_session);
		_pixels = make_icon(0);

		platform::tray_callbacks callbacks;
		callbacks.on_activate = _restore;
		callbacks.on_context_menu = _context_menu;
		callbacks.on_balloon_click = _restore;
		callbacks.on_end_session = [this](const bool session_ending)
		{
			if (session_ending && _end_session)
				_end_session();
		};

		const platform::tray_icon_image image{_pixels, {IconSize, IconSize}};
		_service = platform::create_tray_service(image, "ergo-active", std::move(callbacks));
		if (!_service || !_service->available())
		{
			_diagnostic = _service ? _service->diagnostics() : "The system tray is unavailable.";
			return false;
		}
		if (!_service->show())
		{
			_diagnostic = _service->diagnostics();
			if (_diagnostic.empty())
				_diagnostic = "The system tray rejected publication.";
			return false;
		}
		_current_urgency = 0;
		return true;
	}

	void uninstall() override
	{
		if (_service)
			_service->hide();
		_service.reset();
	}

	void update_urgency(const int level) override
	{
		if (!_service)
			return;
		if (level == _current_urgency)
			return;
		_pixels = make_icon(level);
		const platform::tray_icon_image image{_pixels, {IconSize, IconSize}};
		if (!_service->set_icon(image))
		{
			_diagnostic = _service->diagnostics();
			if (_diagnostic.empty())
				_diagnostic = "The system tray rejected the urgency icon.";
			_current_urgency = -1;
		}
		else
			_current_urgency = level;
	}

	void update_tooltip(const std::string_view text) override
	{
		if (_service && !_service->set_tooltip(text))
			_diagnostic = _service->diagnostics();
	}

	void show_balloon(const std::string_view title,
	                  const std::string_view message,
	                  const std::string_view tip) override
	{
		if (!_service)
			return;
		_service->set_tooltip(tip);
		if (!_service->show_balloon(title, message, platform::tray_balloon_kind::info))
			_diagnostic = _service->diagnostics();
	}

	std::string diagnostics() const override
	{
		return _diagnostic;
	}

private:
	static constexpr int IconSize = 16;
	platform::tray_service_ptr _service;
	std::vector<uint32_t> _pixels;
	std::string _diagnostic;
	int _current_urgency = -1;
	std::function<void()> _restore;
	std::function<void(core::pointi, bool)> _context_menu;
	std::function<void()> _end_session;

	static std::vector<uint32_t> make_icon(const int level)
	{
		const core::color32 color = level >= 2 ? core::rgb(0xE0, 0x40, 0x40)
			: level == 1 ? core::rgb(0xE0, 0xA0, 0x20)
			             : core::rgb(0x4E, 0xC9, 0x6F);
		std::vector<uint32_t> pixels(static_cast<size_t>(IconSize) * IconSize);
		const float half = IconSize * 0.5f;
		const float radius = half - 0.5f;
		for (int y = 0; y < IconSize; ++y)
		{
			for (int x = 0; x < IconSize; ++x)
			{
				const float dx = x + 0.5f - half;
				const float dy = y + 0.5f - half;
				const float distance = std::sqrt(dx * dx + dy * dy);
				if (distance > radius + 0.7f)
					continue;
				const auto alpha = static_cast<uint32_t>(
					std::clamp(radius + 0.5f - distance, 0.0f, 1.0f) * 255.0f);
				pixels[static_cast<size_t>(y) * IconSize + x] = core::abgr(color, alpha);
			}
		}

		// A small white check keeps the status icon legible without loading a
		// native HICON or duplicating the shell's rasterisation.
		for (int i = 0; i < 4; ++i)
		{
			const int x = 5 + i;
			const int y = 9 + i;
			pixels[static_cast<size_t>(y) * IconSize + x] = core::abgr(core::rgb(0xFF, 0xFF, 0xFF));
		}
		for (int i = 0; i < 5; ++i)
		{
			const int x = 8 + i;
			const int y = 12 - i;
			pixels[static_cast<size_t>(y) * IconSize + x] = core::abgr(core::rgb(0xFF, 0xFF, 0xFF));
		}
		return pixels;
	}
};
