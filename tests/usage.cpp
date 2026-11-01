// Behavioural checks for the rolling activity window and the daily statistics in data.h. No
// window or real files are touched: the model uses transient state and an in-memory storage seam.

#include "data.h"

#include <cstdio>
#include <unordered_map>

namespace
{
	class memory_store final : public activity_store
	{
	public:
		activity_store_read_result read_text(const std::string_view name,
		                                     std::string& value) const override
		{
			if (fail_reads)
				return {false, false, "test read failure"};
			const auto found = text.find(std::string(name));
			if (found == text.end())
				return {false, true, {}};
			value = found->second;
			return {};
		}

		activity_store_result write_text_atomic(const std::string_view name,
		                                       const std::string_view value) const override
		{
			if (fail_writes)
				return {false, "test publication failure"};
			text[std::string(name)] = std::string(value);
			return {};
		}

		activity_store_read_result read_binary(const std::string_view name,
		                                       std::vector<uint8_t>& value) const override
		{
			if (fail_reads)
				return {false, false, "test read failure"};
			const auto found = binary.find(std::string(name));
			if (found == binary.end())
				return {false, true, {}};
			value = found->second;
			return {};
		}

		activity_store_result write_binary_atomic(const std::string_view name,
		                                         const std::span<const uint8_t> value) const override
		{
			if (fail_writes)
				return {false, "test publication failure"};
			binary[std::string(name)] = {value.begin(), value.end()};
			return {};
		}

		int64_t seconds_since_written(std::string_view) const override { return 0; }
		void fail_writes_now() const { fail_writes = true; }
		void allow_writes() const { fail_writes = false; }
		void fail_reads_now() const { fail_reads = true; }

	private:
		mutable std::unordered_map<std::string, std::string> text;
		mutable std::unordered_map<std::string, std::vector<uint8_t>> binary;
		mutable bool fail_writes = false;
		mutable bool fail_reads = false;
	};

	int failures = 0;
	int checks = 0;

	void check(const bool condition, const char* description)
	{
		++checks;
		if (!condition) ++failures;
		std::printf("  [%s] %s\n", condition ? "pass" : "FAIL", description);
	}

	void work(usage_data& usage, const int minutes)
	{
		for (int i = 0; i < minutes * TicksPerMinute; ++i)
			usage.step(1, 0);
	}

	void test_gaps_count_as_time_away()
	{
		std::printf("time the timer never saw\n");

		usage_data usage{usage_data::transient};
		work(usage, 10);
		check(usage.get_last_break_minutes() == 10, "ten active minutes are ten minutes without a break");

		usage.age(2 * TicksPerMinute);
		check(usage.get_last_break_minutes() == 12, "a gap shorter than a break extends the stretch");
		check(usage.get_uses()[2 * TicksPerMinute].mouse == 1 && usage.get_uses()[0].mouse == 0,
		      "the window moves the earlier activity back by the gap");

		usage.age(usage_data::ThreeMinutes);
		check(usage.get_last_break_minutes() == 0, "a gap that makes three idle minutes is a break");
		check(usage.get_break_markers()[0] == 1 && usage.get_break_markers()[usage_data::ThreeMinutes - 1] == 1,
		      "the break band covers the gap");

		work(usage, 1);
		check(usage.get_last_break_minutes() == 1, "the stretch restarts from the gap");
	}

	void test_a_break_can_straddle_a_gap()
	{
		std::printf("a break that starts before a gap\n");

		usage_data usage{usage_data::transient};
		work(usage, 5);
		usage.age(2 * TicksPerMinute);

		bool completed = false;
		for (int i = 0; i < TicksPerMinute && !completed; ++i)
			completed = usage.step(0, 0).break_completed;
		check(completed, "it completes once the idle run reaches three minutes");
		check(usage.get_last_break_minutes() == 0, "and resets the stretch");
	}

	void test_long_gaps()
	{
		std::printf("long gaps\n");

		usage_data usage{usage_data::transient};
		work(usage, 30);
		usage.age(int64_t{3650} * 24 * 60 * TicksPerMinute);
		check(usage.get_last_break_minutes() == 0, "ten years away is a break");

		bool empty = true;
		for (int i = 0; i < usage_data::MaxUses; ++i)
			empty = empty && usage.get_uses()[i].mouse == 0 && usage.get_uses()[i].keyboard == 0;
		check(empty, "and leaves no old activity on the graph");

		work(usage, 1);
		check(usage.get_last_break_minutes() == 1 && usage.get_minutes_until_warning(20) == 19,
		      "the next stretch counts normally");

		usage.age(0);
		usage.age(-5);
		check(usage.get_last_break_minutes() == 1, "an empty or negative gap changes nothing");
	}

	void test_a_gap_reports_the_break_it_completes()
	{
		std::printf("breaks completed in a gap are reported\n");

		usage_data usage{usage_data::transient};
		work(usage, 10);
		check(!usage.age(usage_data::ThreeMinutes - 1).break_completed, "a gap one tick short of a break reports none");
		check(usage.age(1).break_completed, "the tick that makes three idle minutes reports the break");
		check(!usage.age(60 * TicksPerMinute).break_completed, "a break that already qualified is not reported twice");

		work(usage, 1);
		const usage_data::step_events lunch = usage.age(60 * TicksPerMinute);
		check(lunch.break_completed, "an hour away after work is a break");
		check(!lunch.micro_pause, "a gap is not counted as a micro-pause");

		check(!usage.age(0).break_completed && !usage.age(-1).break_completed, "an empty gap reports nothing");
	}

	void test_only_a_gap_that_began_today_counts_today()
	{
		std::printf("which day a gap belongs to\n");

		const time_t now = time(nullptr);
		check(usage_data::same_local_day(now, now), "a moment is on its own day");
		check(!usage_data::same_local_day(now - 2 * 24 * 60 * 60, now), "two days ago is another day");
	}

	void test_a_new_day_opens_with_its_score()
	{
		std::printf("a new day's record\n");

		const daily_stats stats{daily_stats::transient};
		check(stats.today().score == 100, "a day with no ticks yet scores 100, not 0");
		check(stats.today().score == daily_stats::compute_score(stats.today(), 0),
		      "and that is the score compute_score gives it");
		check(stats.is_current_day(), "the record is today's");
	}

	void test_history_rows()
	{
		std::printf("history.csv rows\n");

		daily_record r = {};
		check(daily_stats::parse_record("2026-10-02,10,1,5,100,1,2,3,4,5\n", r) &&
		      wcscmp(r.date, L"2026-10-02") == 0 && r.active_minutes == 10 && r.micro_pauses == 5,
		      "a row this version writes is read back");

		r = {};
		check(daily_stats::parse_record("2026-08-19,23,1,20,100,18,125,37,0\n", r) && r.micro_pauses == 0,
		      "a row from before micro-pauses is still read");

		// A date save() could not write back would fail every later save of the file.
		r = {};
		check(!daily_stats::parse_record("\xC3\xBC" "n" "\xC3\xAF" "c" "\xC3\xB6" "d" "\xC3\xA9" ",1,2,3,4,5,6,7,8,9\n", r),
		      "a row with a non-ASCII date is skipped");

		const char* damaged[] = {
			"date,active_minutes,break_count,longest_stretch,score,keyboard_ticks,mouse_ticks,idle_ticks,locked_ticks,micro_pauses\n",
			"garbage line without commas\n",
			",,,,,,,,,\n",
			"2026-1-01,1,2,3,4,5,6,7,8,9\n",
			"2026-10-01x,1,2,3,4,5,6,7,8,9\n",
		};
		bool all_skipped = true;
		for (const char* line : damaged)
		{
			r = {};
			all_skipped = all_skipped && !daily_stats::parse_record(line, r);
		}
		check(all_skipped, "the header, garbage and malformed dates are skipped");
	}

	void test_storage_round_trip()
	{
		std::printf("storage seam round trips\n");
		memory_store store;
		{
			usage_data usage(&store);
			work(usage, 5);
			usage.flush();

			daily_stats stats(&store);
			for (int i = 0; i < TicksPerMinute; ++i)
				stats.record_tick({.keyboard = true, .stretch_minutes = 1, .break_interval = 50});
			stats.flush();
		}

		usage_data usage(&store);
		check(usage.get_last_break_minutes() == 5,
		      "the rolling activity window survives a storage round trip");
		daily_stats stats(&store);
		check(stats.today().active_minutes == 1 && stats.today().keyboard_ticks == TicksPerMinute,
		      "daily statistics survive a storage round trip");

		store.fail_writes_now();
		check(!usage.flush() && usage.last_storage_error() == "test publication failure",
		      "a usage publication failure is returned and retained for notification");
		check(!stats.flush() && stats.last_storage_error() == "test publication failure",
		      "a history publication failure is returned and retained for notification");

		store.allow_writes();
		store.fail_reads_now();
		usage_data failed_usage(&store);
		daily_stats failed_stats(&store);
		check(failed_usage.last_storage_error() == "test read failure" &&
		          failed_stats.last_storage_error() == "test read failure",
		      "non-missing read failures are retained for startup notification");
	}
}

int main()
{
	test_gaps_count_as_time_away();
	test_a_break_can_straddle_a_gap();
	test_long_gaps();
	test_a_gap_reports_the_break_it_completes();
	test_only_a_gap_that_began_today_counts_today();
	test_a_new_day_opens_with_its_score();
	test_history_rows();
	test_storage_round_trip();

	std::printf("\n%s (%d checks)\n",
	            failures == 0 ? "all checks passed" : "there were failures", checks);
	return failures == 0 ? 0 : 1;
}
