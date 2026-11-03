/*
 * unit tests for Meter.cpp
 * Author: Matthias Behr, 2014
 */

#include "gtest/gtest.h"

#include <Buffer.hpp>

namespace {
Reading reading(double value, long sec, long usec = 0) {
	struct timeval t;
	t.tv_sec = sec;
	t.tv_usec = usec;
	return Reading(value, t, ReadingIdentifier::Ptr());
}

// read everything from cursor `from`
std::vector<Buffer::Entry> read_all(Buffer &buf, Buffer::Cursor from = 0, uint64_t *lost = NULL) {
	std::vector<Buffer::Entry> out;
	buf.read(from, 1000000, out, lost);
	return out;
}
} // namespace

TEST(buffer, push_is_staged_until_publish) {
	Buffer buf;
	buf.push(reading(1.0, 1));
	buf.push(reading(2.0, 2));
	EXPECT_EQ(0u, buf.size());
	EXPECT_TRUE(read_all(buf).empty());

	std::vector<Buffer::Entry> published;
	EXPECT_EQ(2u, buf.publish(0, false, &published));
	ASSERT_EQ(2u, published.size());
	EXPECT_EQ(0u, published[0].seq);
	EXPECT_EQ(1000, published[0].time_ms);
	EXPECT_EQ(1.0, published[0].value);
	EXPECT_EQ(1u, published[1].seq);
	EXPECT_EQ(2.0, published[1].value);
	EXPECT_EQ(2u, buf.size());
	EXPECT_TRUE(buf.newValues());

	// publishing again without new readings appends nothing
	EXPECT_EQ(0u, buf.publish(0, false, &published));
	EXPECT_TRUE(published.empty());
	EXPECT_EQ(2u, buf.end_cursor());
}

TEST(buffer, published_only_returns_entries_of_this_call) {
	// local httpd and MQTT are fed from `published`: unacknowledged older entries must not be
	// returned again (they were before: everything not deleted was re-added every cycle)
	Buffer buf;
	std::vector<Buffer::Entry> published;
	buf.push(reading(1.0, 1));
	buf.publish(0, false, &published);
	buf.push(reading(2.0, 2));
	buf.publish(0, false, &published);
	ASSERT_EQ(1u, published.size());
	EXPECT_EQ(2.0, published[0].value);
	EXPECT_EQ(2u, buf.size()); // nothing acknowledged yet, both are kept
}

TEST(buffer, read_does_not_consume_commit_does) {
	Buffer buf;
	for (int i = 1; i <= 5; i++)
		buf.push(reading(i, i));
	buf.publish(0, false);

	std::vector<Buffer::Entry> out;
	EXPECT_EQ(2u, buf.read(0, 2, out));
	EXPECT_EQ(1.0, out[0].value);
	EXPECT_EQ(2.0, out[1].value);
	// reading again (e.g. after a failed request) returns the same entries
	EXPECT_EQ(2u, buf.read(0, 2, out));
	EXPECT_EQ(1.0, out[0].value);
	EXPECT_EQ(5u, buf.size());

	// acknowledge the first two
	buf.commit(out.back().seq + 1);
	EXPECT_EQ(3u, buf.size());
	EXPECT_EQ(3u, buf.read(2, 10, out));
	EXPECT_EQ(3.0, out[0].value);
	EXPECT_EQ(5.0, out[2].value);

	// reading from the end returns nothing
	EXPECT_EQ(0u, buf.read(buf.end_cursor(), 10, out));

	// commit is idempotent and never goes backwards
	buf.commit(1);
	EXPECT_EQ(3u, buf.size());
	buf.commit(buf.end_cursor());
	EXPECT_EQ(0u, buf.size());
}

TEST(buffer, max_size_drops_oldest_and_reports_lost) {
	Buffer buf;
	buf.set_max_size(3);
	for (int i = 1; i <= 5; i++)
		buf.push(reading(i, i));
	buf.publish(0, false);
	EXPECT_EQ(3u, buf.size());

	uint64_t lost = 0;
	std::vector<Buffer::Entry> out = read_all(buf, 0, &lost);
	EXPECT_EQ(2u, lost); // seq 0 and 1 were dropped
	ASSERT_EQ(3u, out.size());
	EXPECT_EQ(2u, out[0].seq);
	EXPECT_EQ(3.0, out[0].value);

	// reading from a cursor that is still available reports no loss
	out = read_all(buf, 3, &lost);
	EXPECT_EQ(0u, lost);
	EXPECT_EQ(2u, out.size());

	// lowering the limit applies immediately
	buf.set_max_size(1);
	EXPECT_EQ(1u, buf.size());
	EXPECT_EQ(5.0, read_all(buf)[0].value);

	// 0 = unlimited
	buf.set_max_size(0);
	for (int i = 6; i <= 20; i++)
		buf.push(reading(i, i));
	buf.publish(0, false);
	EXPECT_EQ(16u, buf.size());
}

TEST(buffer, agg_max_sum) {
	Buffer buf;
	buf.set_aggmode(Buffer::MAX);
	buf.push(reading(-3.0, 1));
	buf.push(reading(-1.0, 3));
	buf.push(reading(-2.0, 2));
	std::vector<Buffer::Entry> published;
	EXPECT_EQ(1u, buf.publish(0, false, &published));
	ASSERT_EQ(1u, published.size());
	EXPECT_EQ(-1.0, published[0].value);
	EXPECT_EQ(3000, published[0].time_ms); // timestamp of the latest reading

	buf.set_aggmode(Buffer::SUM);
	buf.push(reading(1.0, 4));
	buf.push(reading(2.5, 5));
	EXPECT_EQ(1u, buf.publish(0, false, &published));
	EXPECT_EQ(3.5, published[0].value);
	EXPECT_EQ(5000, published[0].time_ms);
}

TEST(buffer, buffer_agg_avg) {
	Buffer buf;
	buf.set_aggmode(Buffer::AVG);
	std::vector<Buffer::Entry> published;

	// first case: no prev. value, just one data -> return value as AVG.
	buf.push(reading(1.0, 1));
	buf.publish(0, false, &published);
	ASSERT_EQ(1u, published.size());
	EXPECT_EQ(1.0, published[0].value);

	// 2nd case: prev. value (1.0 at 1s), just one new data (2.0 at 2s)-> return 1.0 as AVG (2.0
	// has no time yet!)
	buf.push(reading(2.0, 2));
	buf.publish(0, false, &published);
	ASSERT_EQ(1u, published.size());
	EXPECT_EQ(1.0, published[0].value);

	// 3rd case: prev. value (2.0 at 2s), two new data (3.0 at 4s and 4.0 at 7s)-> return
	// (2*2+3*3)/5 as AVG (4.0 has no time yet!)
	buf.push(reading(3.0, 4));
	buf.push(reading(4.0, 7));
	buf.publish(0, false, &published);
	ASSERT_EQ(1u, published.size());
	EXPECT_EQ(((2.0 * 2.0) + (3.0 * 3.0)) / 5.0, published[0].value);
	EXPECT_EQ(7000, published[0].time_ms);
}

TEST(buffer, aggregation_never_touches_published_entries) {
	// Before, aggregate() merged everything not yet deleted, including results of previous
	// periods that the sender had not managed to transmit (e.g. during a hanging request).
	Buffer buf;
	buf.set_aggmode(Buffer::SUM);
	buf.push(reading(1.0, 1));
	buf.push(reading(1.0, 2));
	buf.publish(0, false); // period 1: 2.0
	buf.push(reading(5.0, 3));
	buf.publish(0, false); // period 2: 5.0, period 1 still unacknowledged

	std::vector<Buffer::Entry> out = read_all(buf);
	ASSERT_EQ(2u, out.size());
	EXPECT_EQ(2.0, out[0].value);
	EXPECT_EQ(5.0, out[1].value);
}

TEST(buffer, agg_fixed_interval) {
	Buffer buf;
	buf.set_aggmode(Buffer::MAX);
	buf.push(reading(1.0, 1234, 567000));
	std::vector<Buffer::Entry> published;
	buf.publish(60, true, &published);
	ASSERT_EQ(1u, published.size());
	EXPECT_EQ(1200000, published[0].time_ms);

	// without aggmode, timestamps are not touched (as before)
	buf.set_aggmode(Buffer::NONE);
	buf.push(reading(1.0, 1234, 567000));
	buf.publish(60, true, &published);
	EXPECT_EQ(1234567, published[0].time_ms);
}

TEST(buffer, dump) {
	Buffer buf;
	EXPECT_EQ("{}", buf.dump());
	buf.push(reading(1.5, 1));
	buf.push(reading(2.0, 2));
	buf.publish(0, false);
	EXPECT_EQ("{1.5,2}", buf.dump());
}
