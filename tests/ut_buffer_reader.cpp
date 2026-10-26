#include "gtest/gtest.h"

#include <Buffer.hpp>
#include <api/BufferReader.hpp>

using vz::api::BufferReader;

namespace {
void publish(Buffer::Ptr buf, double value, int64_t time_ms) {
	struct timeval t;
	t.tv_sec = time_ms / 1000;
	t.tv_usec = (time_ms % 1000) * 1000;
	buf->push(Reading(value, t, ReadingIdentifier::Ptr()));
	buf->publish(0, false);
}
} // namespace

TEST(buffer_reader, failed_send_is_retried_and_kept_in_buffer) {
	Buffer::Ptr buf(new Buffer());
	BufferReader r(buf, "test", 0, false);
	EXPECT_TRUE(r.next(10).empty());

	publish(buf, 1.0, 1000);
	publish(buf, 2.0, 2000);
	ASSERT_EQ(2u, r.next(10).size());
	// request failed: nothing committed. More data arrives meanwhile:
	publish(buf, 3.0, 3000);
	const std::vector<Buffer::Entry> &b = r.next(10);
	ASSERT_EQ(3u, b.size());
	EXPECT_EQ(1.0, b[0].value);
	EXPECT_EQ(3.0, b[2].value);
	EXPECT_EQ(3u, buf->size());

	r.commit();
	EXPECT_EQ(0u, buf->size());
	EXPECT_TRUE(r.next(10).empty());
}

TEST(buffer_reader, batches_respect_max) {
	Buffer::Ptr buf(new Buffer());
	BufferReader r(buf, "test", 0, false);
	for (int i = 1; i <= 25; i++)
		publish(buf, i, i * 1000);

	EXPECT_EQ(10u, r.next(10).size());
	r.commit();
	EXPECT_EQ(15u, buf->size());
	EXPECT_EQ(11.0, r.next(10)[0].value);
	r.commit();
	EXPECT_EQ(5u, r.next(10).size());
	r.commit();
	EXPECT_EQ(0u, buf->size());
}

TEST(buffer_reader, filter_state_only_advances_on_commit) {
	// Before, the timestamp of an unsent reading was remembered while building the request, so
	// after a failure all older unsent readings were skipped as "too old".
	Buffer::Ptr buf(new Buffer());
	BufferReader r(buf, "test", 0, false);
	publish(buf, 1.0, 1000);
	publish(buf, 2.0, 2000);
	EXPECT_EQ(2u, r.next(10).size()); // fails
	EXPECT_EQ(2u, r.next(10).size()); // retried with the same readings
	r.commit();

	// older or equal timestamps than what was sent are skipped (and acknowledged)
	publish(buf, 3.0, 2000);
	publish(buf, 4.0, 1500);
	EXPECT_TRUE(r.next(10).empty());
	EXPECT_EQ(0u, buf->size());
}

TEST(buffer_reader, equal_timestamps_allowed) {
	Buffer::Ptr buf(new Buffer());
	BufferReader r(buf, "test", 0, true);
	publish(buf, 1.0, 1000);
	publish(buf, 2.0, 1000);
	publish(buf, 3.0, 999);
	const std::vector<Buffer::Entry> &b = r.next(10);
	ASSERT_EQ(2u, b.size());
	EXPECT_EQ(2.0, b[1].value);
}

TEST(buffer_reader, duplicates) {
	Buffer::Ptr buf(new Buffer());
	BufferReader r(buf, "test", 10, false);
	publish(buf, 1.0, 1000);  // first: sent
	publish(buf, 1.0, 2000);  // same value within 10s: skipped
	publish(buf, 2.0, 3000);  // value changed: sent
	publish(buf, 2.0, 13000); // same value but 10s passed: sent
	publish(buf, 2.0, 14000); // skipped
	const std::vector<Buffer::Entry> &b = r.next(10);
	ASSERT_EQ(3u, b.size());
	EXPECT_EQ(1000, b[0].time_ms);
	EXPECT_EQ(3000, b[1].time_ms);
	EXPECT_EQ(13000, b[2].time_ms);
	r.commit();
	EXPECT_EQ(0u, buf->size()); // skipped ones are acknowledged with the batch

	// only a skipped one pending: acknowledged right away, nothing to send
	publish(buf, 2.0, 15000);
	EXPECT_TRUE(r.next(10).empty());
	EXPECT_EQ(0u, buf->size());

	// a batch fills up with entries that pass the filter, skipping the others
	for (int i = 0; i < 6; i++)
		publish(buf, (i < 3) ? 3.0 : 4.0, 16000 + i * 1000);
	// values: 3,3,3,4,4,4 -> sent: 3@16000, 4@19000
	const std::vector<Buffer::Entry> &b2 = r.next(2);
	ASSERT_EQ(2u, b2.size());
	EXPECT_EQ(16000, b2[0].time_ms);
	EXPECT_EQ(19000, b2[1].time_ms);
}

TEST(buffer_reader, commit_first) {
	Buffer::Ptr buf(new Buffer());
	BufferReader r(buf, "test", 0, false);
	publish(buf, 1.0, 1000);
	publish(buf, 2.0, 2000);
	publish(buf, 3.0, 3000);
	ASSERT_EQ(3u, r.next(10).size());
	r.commit_first(); // middleware rejected the first one as duplicate
	EXPECT_EQ(2u, buf->size());
	const std::vector<Buffer::Entry> &b = r.next(10);
	ASSERT_EQ(2u, b.size());
	EXPECT_EQ(2.0, b[0].value);

	r.commit_first();
	r.commit_first(); // batch is cleared after a commit: no-op
	EXPECT_EQ(1u, buf->size());
}

TEST(buffer_reader, dropped_entries_are_skipped) {
	Buffer::Ptr buf(new Buffer());
	buf->set_max_size(3);
	BufferReader r(buf, "test", 0, false);
	for (int i = 1; i <= 5; i++)
		publish(buf, i, i * 1000);
	const std::vector<Buffer::Entry> &b = r.next(10);
	ASSERT_EQ(3u, b.size());
	EXPECT_EQ(3.0, b[0].value);
	r.commit();
	EXPECT_EQ(0u, buf->size());
}
