// data.h - Activity model and persistence.
//
// daily_stats persists per-day usage statistics to %APPDATA%\ergo-active\history.csv.
// usage_data holds the rolling activity window backing the dashboard graph and
// drives break / micro-pause detection.

#pragma once

// Input is sampled on a timer; this many samples make one minute.
inline constexpr int TicksPerMinute = 6;

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

	daily_stats()
	{
		resolve_path();
		load();
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

	void flush()
	{
		save();
	}

	const daily_record& today() const
	{
		return _records.back();
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

private:
	std::vector<daily_record> _records;
	wchar_t _csv_path[MAX_PATH] = {};
	int _tick_accumulator = 0;
	int _break_interval = 0;

	void resolve_path()
	{
		wchar_t appdata[MAX_PATH] = {};
		if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
			return;

		wchar_t dir_path[MAX_PATH] = {};
		swprintf_s(dir_path, L"%s\\ergo-active", appdata);

		if (!CreateDirectoryW(dir_path, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
			return;

		swprintf_s(_csv_path, L"%s\\ergo-active\\history.csv", appdata);
	}

	static void get_today_string(wchar_t* buf, const size_t len)
	{
		const time_t now = time(nullptr);
		tm lt = {};
		localtime_s(&lt, &now);
		swprintf_s(buf, len, L"%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
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

		if (_records.empty() || wcscmp(_records.back().date, today_str) != 0)
		{
			daily_record r = {};
			wcscpy_s(r.date, today_str);
			_records.push_back(r);
			trim_old_records();
		}
	}

	void load()
	{
		if (_csv_path[0] == L'\0')
			return;

		FILE* f = nullptr;
		if (_wfopen_s(&f, _csv_path, L"r") != 0 || f == nullptr)
			return;

		char line[256] = {};
		// Skip header
		if (fgets(line, sizeof(line), f) == nullptr)
		{
			fclose(f);
			return;
		}

		while (fgets(line, sizeof(line), f) != nullptr)
		{
			daily_record r = {};
			char date_buf[16] = {};
			// Files written before micro_pauses was added have 9 fields; accept those too.
			const int fields = sscanf_s(line, "%15[^,],%d,%d,%d,%d,%d,%d,%d,%d,%d",
			                            date_buf, static_cast<unsigned>(sizeof(date_buf)),
			                            &r.active_minutes, &r.break_count, &r.longest_stretch, &r.score,
			                            &r.keyboard_ticks, &r.mouse_ticks, &r.idle_ticks, &r.locked_ticks,
			                            &r.micro_pauses);
			if (fields >= 9)
			{
				// Convert date to wide
				for (int i = 0; i < 15 && date_buf[i]; i++)
					r.date[i] = static_cast<wchar_t>(date_buf[i]);

				_records.push_back(r);
			}
		}

		fclose(f);
		trim_old_records();
	}

	void save() const
	{
		if (_csv_path[0] == L'\0')
			return;

		// Write to a temporary file first, then atomically rename
		wchar_t tmp_path[MAX_PATH] = {};
		swprintf_s(tmp_path, L"%s.tmp", _csv_path);

		FILE* f = nullptr;
		if (_wfopen_s(&f, tmp_path, L"w") != 0 || f == nullptr)
			return;

		bool ok = fprintf(
			f,
			"date,active_minutes,break_count,longest_stretch,score,keyboard_ticks,mouse_ticks,idle_ticks,locked_ticks,micro_pauses\n")
			> 0;

		for (const auto& r : _records)
		{
			if (!ok) break;
			ok = fprintf(f, "%ls,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			             r.date, r.active_minutes, r.break_count, r.longest_stretch, r.score,
			             r.keyboard_ticks, r.mouse_ticks, r.idle_ticks, r.locked_ticks, r.micro_pauses) > 0;
		}

		fclose(f);

		if (ok)
			MoveFileExW(tmp_path, _csv_path, MOVEFILE_REPLACE_EXISTING);
		else
			DeleteFileW(tmp_path);
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

	usage_data()
	{
		resolve_path();
		load();
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

	void flush()
	{
		save();
	}

private:
	static constexpr uint32_t FileVersion = 2;

	usage_tick _uses[MaxUses]{};
	uint8_t _break_markers[MaxUses]{};
	int _last_break = 0;
	int _now_break = 0;
	wchar_t _dat_path[MAX_PATH] = {};

	void resolve_path()
	{
		wchar_t appdata[MAX_PATH] = {};
		if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
			return;

		wchar_t dir_path[MAX_PATH] = {};
		swprintf_s(dir_path, L"%s\\ergo-active", appdata);
		CreateDirectoryW(dir_path, nullptr);

		swprintf_s(_dat_path, L"%s\\ergo-active\\usage.dat", appdata);
	}

	void save() const
	{
		if (_dat_path[0] == L'\0')
			return;

		wchar_t tmp_path[MAX_PATH] = {};
		swprintf_s(tmp_path, L"%s.tmp", _dat_path);

		FILE* f = nullptr;
		if (_wfopen_s(&f, tmp_path, L"wb") != 0 || f == nullptr)
			return;

		bool ok = true;
		const uint32_t ver = FileVersion;
		ok = ok && fwrite(&ver, sizeof(ver), 1, f) == 1;
		ok = ok && fwrite(_uses, sizeof(_uses), 1, f) == 1;
		ok = ok && fwrite(_break_markers, sizeof(_break_markers), 1, f) == 1;
		ok = ok && fwrite(&_last_break, sizeof(_last_break), 1, f) == 1;
		ok = ok && fwrite(&_now_break, sizeof(_now_break), 1, f) == 1;

		fclose(f);

		if (ok)
			MoveFileExW(tmp_path, _dat_path, MOVEFILE_REPLACE_EXISTING);
		else
			DeleteFileW(tmp_path);
	}

	void load()
	{
		if (_dat_path[0] == L'\0')
			return;

		FILE* f = nullptr;
		if (_wfopen_s(&f, _dat_path, L"rb") != 0 || f == nullptr)
			return;

		uint32_t ver = 0;
		if (fread(&ver, sizeof(ver), 1, f) != 1 || ver != FileVersion)
		{
			fclose(f);
			return;
		}

		bool ok = true;
		ok = ok && fread(_uses, sizeof(_uses), 1, f) == 1;
		ok = ok && fread(_break_markers, sizeof(_break_markers), 1, f) == 1;
		ok = ok && fread(&_last_break, sizeof(_last_break), 1, f) == 1;
		ok = ok && fread(&_now_break, sizeof(_now_break), 1, f) == 1;

		fclose(f);

		if (!ok)
		{
			// Reset to clean state on partial read
			memset(_uses, 0, sizeof(_uses));
			memset(_break_markers, 0, sizeof(_break_markers));
			_last_break = 0;
			_now_break = 0;
		}
	}
};
