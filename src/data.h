// data.h - Platform-neutral activity model and persistence contract.
//
// The model owns the rolling activity window and the daily statistics.  The
// application supplies the small storage seam below; Windows file and clock
// details stay in the app-native backend, not in this model or in the UI.

#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cwchar>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iterator>
#include <limits>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Input is sampled on a timer; this many samples make one minute.
inline constexpr int TicksPerMinute = 6;

struct activity_store_result
{
	bool success = true;
	std::string diagnostic;

	explicit operator bool() const { return success; }
};

struct activity_store_read_result
{
	bool success = true;
	bool not_found = false;
	std::string diagnostic;

	explicit operator bool() const { return success; }
};

struct activity_store
{
	virtual ~activity_store() = default;

	virtual activity_store_read_result read_text(std::string_view name, std::string& value) const = 0;
	virtual activity_store_result write_text_atomic(std::string_view name,
	                                                std::string_view value) const = 0;
	virtual activity_store_read_result read_binary(std::string_view name,
	                                               std::vector<uint8_t>& value) const = 0;
	virtual activity_store_result write_binary_atomic(std::string_view name,
	                                                  std::span<const uint8_t> value) const = 0;

	// Seconds since the file was written.  Missing or unreadable files return
	// zero, which makes a restart with no prior state behave like a fresh run.
	virtual int64_t seconds_since_written(std::string_view name) const = 0;
};

struct daily_record
{
	wchar_t date[16] = {}; // YYYY-MM-DD
	int active_minutes = 0;
	int break_count = 0;
	int longest_stretch = 0; // minutes without a qualifying break
	int score = 0;
	int keyboard_ticks = 0;
	int mouse_ticks = 0;
	int idle_ticks = 0;
	int locked_ticks = 0;
	int micro_pauses = 0;
};

struct tick_sample
{
	bool keyboard = false;
	bool mouse = false;
	bool locked = false;
	int stretch_minutes = 0; // minutes since the last qualifying break
	int break_interval = 0; // the user's configured break interval, in minutes
};

class daily_stats
{
public:
	static constexpr int MaxHistory = 30;

	daily_stats() { ensure_today(); }

	explicit daily_stats(activity_store* store) : _store(store)
	{
		if (_store != nullptr)
			load();
		ensure_today();
	}

	// Neither loads nor saves, so tests cannot touch the user's history file.
	struct transient_t
	{
		explicit transient_t() = default;
	};

	static constexpr transient_t transient{};

	explicit daily_stats(transient_t)
	{
		ensure_today();
	}

	~daily_stats()
	{
		save();
	}

	void record_tick(const tick_sample& sample)
	{
		ensure_today();
		auto& today = _records.back();

		if (sample.locked)
		{
			today.locked_ticks++;
		}
		else if (sample.keyboard || sample.mouse)
		{
			_tick_accumulator++;
			if (_tick_accumulator >= TicksPerMinute)
			{
				today.active_minutes++;
				_tick_accumulator = 0;
			}

			if (sample.keyboard)
				today.keyboard_ticks++;
			if (sample.mouse)
				today.mouse_ticks++;
		}
		else
		{
			today.idle_ticks++;
		}

		if (sample.stretch_minutes > today.longest_stretch)
			today.longest_stretch = sample.stretch_minutes;

		_break_interval = sample.break_interval;
		today.score = compute_score(today, _break_interval);
	}

	void record_break()
	{
		ensure_today();
		auto& today = _records.back();
		today.break_count++;
		today.score = compute_score(today, _break_interval);
	}

	void record_micro_pause()
	{
		ensure_today();
		_records.back().micro_pauses++;
	}

	bool flush()
	{
		return save();
	}

	[[nodiscard]] const std::string& last_storage_error() const
	{
		return _last_storage_error;
	}

	const daily_record& today() const
	{
		return _records.back();
	}

	// False once midnight has passed since the last record was made: today() is then still
	// the previous day's record until the next tick creates a new one.
	bool is_current_day() const
	{
		wchar_t today_str[16] = {};
		get_today_string(today_str, 16);
		return !_records.empty() && std::wcscmp(_records.back().date, today_str) == 0;
	}

	const std::vector<daily_record>& history() const
	{
		return _records;
	}

	// Score 0-100, measured against the break interval the user actually chose.
	static int compute_score(const daily_record& r, const int break_interval)
	{
		const int limit = break_interval > 0 ? break_interval : 30;
		int score = 100;

		if (r.longest_stretch > limit)
			score -= (r.longest_stretch - limit) * 2;

		if (r.active_minutes > limit && r.break_count == 0)
			score -= 20;

		score += std::min(r.break_count * 3, 30);

		return std::clamp(score, 0, 100);
	}

	// One row of history.csv into `r`; false for a header, garbage or a damaged row. The date
	// must be YYYY-MM-DD: it is written back as text, and one row save() cannot write - a
	// non-ASCII date - fails the whole save, which would stop history being kept at all.
	static bool parse_record(const char* line, daily_record& r)
	{
		std::array<std::string_view, 10> fields = {};
		const std::string_view input(line);
		size_t start = 0;
		size_t field_count = 0;
		while (field_count < fields.size())
		{
			const size_t comma = input.find(',', start);
			fields[field_count++] = input.substr(start,
				comma == std::string_view::npos ? input.size() - start : comma - start);
			if (comma == std::string_view::npos)
				break;
			start = comma + 1;
		}

		if (field_count < 9 || fields[0].size() >= 16)
			return false;

		char date_buf[16] = {};
		std::memcpy(date_buf, fields[0].data(), fields[0].size());
		if (!is_iso_date(date_buf))
			return false;

		const auto parse_int = [](std::string_view text, int& value)
		{
			while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
				text.remove_suffix(1);
			if (text.empty())
				return false;
			const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
			return result.ec == std::errc{} && result.ptr == text.data() + text.size();
		};

		int* values[] = {
			&r.active_minutes, &r.break_count, &r.longest_stretch, &r.score,
			&r.keyboard_ticks, &r.mouse_ticks, &r.idle_ticks, &r.locked_ticks,
		};
		for (size_t i = 0; i < 8; ++i)
			if (!parse_int(fields[i + 1], *values[i]))
				return false;
		if (field_count >= 10 && !parse_int(fields[9], r.micro_pauses))
			return false;

		for (size_t i = 0; i < fields[0].size(); ++i)
			r.date[i] = static_cast<wchar_t>(date_buf[i]);
		return true;
	}

	static bool is_iso_date(const char* text)
	{
		for (int i = 0; i < 10; i++)
		{
			const bool dash = i == 4 || i == 7;
			if (dash ? text[i] != '-' : (text[i] < '0' || text[i] > '9'))
				return false;
		}
		return text[10] == '\0';
	}

private:
	activity_store* _store = nullptr;
	std::vector<daily_record> _records;
	int _tick_accumulator = 0;
	int _break_interval = 0;
	mutable std::string _last_storage_error;

	static std::tm local_time(const time_t value)
	{
		std::tm result = {};
#if defined(_MSC_VER)
		::localtime_s(&result, &value);
#else
		if (const auto* const local = std::localtime(&value))
			result = *local;
#endif
		return result;
	}

	static void get_today_string(wchar_t* buf, const size_t len)
	{
		const auto lt = local_time(std::time(nullptr));
		std::swprintf(buf, len, L"%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
	}

	void trim_old_records()
	{
		if (_records.size() > MaxHistory)
			_records.erase(_records.begin(), _records.begin() + static_cast<ptrdiff_t>(_records.size() - MaxHistory));
	}

	void ensure_today()
	{
		wchar_t today_str[16] = {};
		get_today_string(today_str, 16);

		if (_records.empty() || std::wcscmp(_records.back().date, today_str) != 0)
		{
			daily_record r = {};
			std::copy(std::begin(today_str), std::end(today_str), std::begin(r.date));
			// Scored now, not at the first tick: the dashboard shows the score as soon as it
			// opens, and an unscored day would read as a red 0 until then.
			r.score = compute_score(r, _break_interval);
			_records.push_back(r);
			trim_old_records();
		}
	}

	void load()
	{
		if (_store == nullptr)
			return;

		std::string contents;
		const auto result = _store->read_text("history.csv", contents);
		if (!result)
		{
			if (!result.not_found)
				_last_storage_error = result.diagnostic;
			return;
		}

		size_t line_start = 0;
		bool first = true;
		while (line_start <= contents.size())
		{
			const size_t line_end = contents.find('\n', line_start);
			const size_t count = line_end == std::string::npos ? contents.size() - line_start
			                                                     : line_end - line_start;
			if (!first && count > 0)
			{
				std::string line = contents.substr(line_start, count);
				daily_record r = {};
				if (parse_record(line.c_str(), r))
					_records.push_back(r);
			}
			first = false;
			if (line_end == std::string::npos)
				break;
			line_start = line_end + 1;
		}

		trim_old_records();
	}

	bool save() const
	{
		if (_store == nullptr)
			return true;

		std::ostringstream out;
		out << "date,active_minutes,break_count,longest_stretch,score,keyboard_ticks,mouse_ticks,"
		       "idle_ticks,locked_ticks,micro_pauses\n";

		for (const auto& r : _records)
		{
			std::string date;
			for (const wchar_t c : r.date)
			{
				if (c == L'\0')
					break;
				if (c > 0x7f)
				{
					_last_storage_error = "history.csv contains a non-ASCII date";
					return false;
				}
				date += static_cast<char>(c);
			}

			out << date << ',' << r.active_minutes << ',' << r.break_count << ','
			    << r.longest_stretch << ',' << r.score << ',' << r.keyboard_ticks << ','
			    << r.mouse_ticks << ',' << r.idle_ticks << ',' << r.locked_ticks << ','
			    << r.micro_pauses << '\n';
			if (!out)
			{
				_last_storage_error = "could not format history.csv";
				return false;
			}
		}

		const auto csv = out.str();
		const auto result = _store->write_text_atomic("history.csv", csv);
		_last_storage_error = result.success ? std::string{} : result.diagnostic;
		return result.success;
	}
};

struct usage_tick
{
	int64_t mouse = 0;
	int64_t keyboard = 0;
};

class usage_data
{
public:
	static constexpr int MaxMinutes = 240;
	static constexpr int TimerGap = TicksPerMinute;
	static constexpr int ThreeMinutes = 3 * TimerGap;
	static constexpr int MaxUses = ThreeMinutes + (MaxMinutes * TimerGap);
	static constexpr int MicroPauseTicks = 2; // ~20 seconds of no input

	struct step_events
	{
		bool break_completed = false;
		bool micro_pause = false;
	};

	usage_data() = default;

	explicit usage_data(activity_store* store) : _store(store)
	{
		if (_store != nullptr)
			load();
	}

	// Neither loads nor saves, so tests cannot touch the user's usage file.
	struct transient_t
	{
		explicit transient_t() = default;
	};

	static constexpr transient_t transient{};

	explicit usage_data(transient_t)
	{
	}

	~usage_data()
	{
		save();
	}

	step_events step(const int64_t mouse, const int64_t keyboard)
	{
		step_events events;

		std::shift_right(std::begin(_uses), std::end(_uses), 1);
		std::shift_right(std::begin(_break_markers), std::end(_break_markers), 1);

		_last_break += 1;

		const bool idle = (mouse + keyboard) == 0;

		if (idle)
		{
			_now_break += 1;

			if (_now_break == MicroPauseTicks)
				events.micro_pause = true;

			if (_now_break == ThreeMinutes)
				events.break_completed = true;

			if (_now_break >= ThreeMinutes)
				_last_break = 0;
		}
		else
		{
			_now_break = 0;
		}

		_uses[0] = {mouse, keyboard};

		// A break only qualifies once it reaches ThreeMinutes, so backfill the
		// whole idle run to make the band cover the break from its true start.
		_break_markers[0] = 0;
		if (_now_break >= ThreeMinutes)
			std::fill_n(_break_markers, std::min(_now_break, MaxUses), static_cast<uint8_t>(1));

		return events;
	}

	// Advances the window over time in which the timer never fired - the app closed, the
	// machine asleep - as idle ticks, which is what that time was. Without it a restart or
	// a sleep resumes the old stretch as if no time had passed, and the graph shows hours-old
	// activity as the last few minutes.
	//
	// Reports a break when the gap completes one, as step() would have tick by tick, so it can
	// be counted. Micro-pauses are left to the ticks the timer sees: a short gap is usually
	// the app being restarted, not a pause in the work.
	step_events age(const int64_t ticks)
	{
		step_events events;
		if (ticks <= 0)
			return events;

		const int shift = static_cast<int>(std::min<int64_t>(ticks, MaxUses));
		std::shift_right(std::begin(_uses), std::end(_uses), shift);
		std::fill_n(_uses, shift, usage_tick{});
		std::shift_right(std::begin(_break_markers), std::end(_break_markers), shift);
		std::fill_n(_break_markers, shift, static_cast<uint8_t>(0));

		// Capped: past a full window the idle run's length no longer changes anything.
		const int idle_before = _now_break;
		_now_break = static_cast<int>(std::min<int64_t>(_now_break + ticks, MaxUses + ThreeMinutes));
		if (_now_break >= ThreeMinutes)
		{
			events.break_completed = idle_before < ThreeMinutes;
			_last_break = 0;
			std::fill_n(_break_markers, std::min(_now_break, MaxUses), static_cast<uint8_t>(1));
		}
		else
		{
			_last_break += static_cast<int>(ticks);
		}

		return events;
	}

	// A break completed while the app was closed, reported once. Only a gap that began today
	// counts: one that began on an earlier day belongs to that day's statistics.
	bool take_break_while_closed()
	{
		const bool taken = _break_while_closed;
		_break_while_closed = false;
		return taken;
	}

	static bool same_local_day(const time_t a, const time_t b)
	{
		const auto la = local_time(a);
		const auto lb = local_time(b);
		return la.tm_year == lb.tm_year && la.tm_yday == lb.tm_yday;
	}

	const usage_tick* get_uses() const { return _uses; }
	const uint8_t* get_break_markers() const { return _break_markers; }

	int get_last_break_minutes() const { return _last_break / TimerGap; }

	bool is_active() const
	{
		return (_uses[0].mouse + _uses[0].keyboard) > 0;
	}

	// Tray icon urgency level: 0=green, 1=yellow, 2=red
	int get_urgency_level(const int delay_minutes) const
	{
		const int mins_since_break = get_last_break_minutes();
		if (mins_since_break >= delay_minutes)
			return 2;
		if (mins_since_break >= delay_minutes * 3 / 4)
			return 1;
		return 0;
	}

	int get_minutes_until_warning(const int delay_minutes) const
	{
		return std::max(0, delay_minutes - get_last_break_minutes());
	}

	// 20-20-20 rule: fires every 20 minutes of continuous activity
	bool should_show_eye_reminder() const
	{
		constexpr int TwentyMinutes = 20 * TimerGap;
		if (_last_break == 0)
			return false;
		return (_last_break % TwentyMinutes) == 0 && is_active();
	}

	bool flush()
	{
		return save();
	}

	[[nodiscard]] const std::string& last_storage_error() const
	{
		return _last_storage_error;
	}

private:
	static constexpr uint32_t FileVersion = 2;
	static constexpr int SecondsPerTick = 60 / TimerGap;

	activity_store* _store = nullptr;
	usage_tick _uses[MaxUses]{};
	uint8_t _break_markers[MaxUses]{};
	int _last_break = 0;
	int _now_break = 0;
	bool _break_while_closed = false;
	mutable std::string _last_storage_error;

	static std::tm local_time(const time_t value)
	{
		std::tm result = {};
#if defined(_MSC_VER)
		::localtime_s(&result, &value);
#else
		if (const auto* const local = std::localtime(&value))
			result = *local;
#endif
		return result;
	}

	bool save() const
	{
		if (_store == nullptr)
			return true;

		std::vector<uint8_t> bytes;
		bytes.reserve(sizeof(FileVersion) + sizeof(_uses) + sizeof(_break_markers) +
		              sizeof(_last_break) + sizeof(_now_break));

		const auto append = [&bytes](const void* data, const size_t size)
		{
			const auto old_size = bytes.size();
			bytes.resize(old_size + size);
			std::memcpy(bytes.data() + old_size, data, size);
		};

		const uint32_t ver = FileVersion;
		append(&ver, sizeof(ver));
		append(_uses, sizeof(_uses));
		append(_break_markers, sizeof(_break_markers));
		append(&_last_break, sizeof(_last_break));
		append(&_now_break, sizeof(_now_break));
		const auto result = _store->write_binary_atomic("usage.dat", bytes);
		_last_storage_error = result.success ? std::string{} : result.diagnostic;
		return result.success;
	}

	void load()
	{
		if (_store == nullptr)
			return;

		std::vector<uint8_t> bytes;
		const auto result = _store->read_binary("usage.dat", bytes);
		if (!result)
		{
			if (!result.not_found)
				_last_storage_error = result.diagnostic;
			return;
		}

		const size_t expected = sizeof(FileVersion) + sizeof(_uses) + sizeof(_break_markers) +
		                        sizeof(_last_break) + sizeof(_now_break);
		if (bytes.size() < expected)
			return;

		size_t offset = 0;
		const auto read = [&bytes, &offset](void* data, const size_t size)
		{
			std::memcpy(data, bytes.data() + offset, size);
			offset += size;
		};

		uint32_t ver = 0;
		read(&ver, sizeof(ver));
		if (ver != FileVersion)
			return;

		read(_uses, sizeof(_uses));
		read(_break_markers, sizeof(_break_markers));
		read(&_last_break, sizeof(_last_break));
		read(&_now_break, sizeof(_now_break));

		const int64_t away = std::max<int64_t>(0, _store->seconds_since_written("usage.dat"));
		const step_events gap = age(away / SecondsPerTick);
		const time_t now = std::time(nullptr);
		_break_while_closed = gap.break_completed &&
			same_local_day(now - static_cast<time_t>(away), now);
	}
};
