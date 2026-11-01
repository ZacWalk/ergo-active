// dashboard-policy.h - Headless control and notification policy.

#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

class delay_control
{
public:
	delay_control(const int minimum, const int maximum, const int initial)
		: _minimum(minimum), _maximum(std::max(minimum, maximum)), _value(initial)
	{
		set_value(initial);
	}

	[[nodiscard]] int value() const { return _value; }
	[[nodiscard]] std::string_view text() const { return _text; }

	void set_value(const int value)
	{
		_value = std::clamp(value, _minimum, _maximum);
		_text = std::to_string(_value);
	}

	void nudge(const int delta)
	{
		set_value(_value + delta);
	}

	// Mirrors the old edit control: the accepted slider value is clamped
	// immediately, while the literal text remains until focus is lost.
	void text_changed(const std::string_view text)
	{
		_text.assign(text);
		int parsed = 0;
		const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
		if (result.ec == std::errc{} && result.ptr == text.data() + text.size())
			_value = std::clamp(parsed, _minimum, _maximum);
		else
			_value = _minimum;
	}

	void commit()
	{
		_text = std::to_string(_value);
	}

private:
	int _minimum;
	int _maximum;
	int _value;
	std::string _text;
};

enum class reminder_kind
{
	none,
	break_warning,
	eye_strain,
};

inline reminder_kind choose_reminder(const bool can_show,
                                     const int last_break_minutes,
                                     const int delay_minutes,
                                     const bool eye_due)
{
	if (!can_show)
		return reminder_kind::none;
	if (last_break_minutes > delay_minutes)
		return reminder_kind::break_warning;
	if (eye_due)
		return reminder_kind::eye_strain;
	return reminder_kind::none;
}

inline int urgency_level(const int last_break_minutes, const int delay_minutes)
{
	if (last_break_minutes >= delay_minutes)
		return 2;
	if (last_break_minutes >= delay_minutes * 3 / 4)
		return 1;
	return 0;
}

inline uint64_t elapsed_periods(const double now, const double previous, const double period)
{
	if (!(period > 0.0) || now <= previous)
		return 0;
	return static_cast<uint64_t>(std::floor((now - previous) / period));
}

struct input_policy_result
{
	int64_t mouse = 0;
	int64_t keyboard = 0;
};

inline input_policy_result classify_input(const bool last_input_changed,
                                          const bool cursor_moved,
                                          const bool cursor_query_succeeded,
                                          const bool locked)
{
	if (!last_input_changed || !cursor_query_succeeded || locked)
		return {};
	return cursor_moved ? input_policy_result{1, 0} : input_policy_result{0, 1};
}
