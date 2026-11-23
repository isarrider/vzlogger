#include <regex>

#include <Buffer.hpp>
#include <Channel.hpp>
#include <Config_Options.hpp>
#include <api/Volkszaehler.hpp>
// #include <api/CurlResponse.hpp>

#include "gtest/gtest.h"
#include <test_config.hpp>

// another dirty hack to test private functions from Volkszaehler:
namespace vz {
namespace api {
class Volkszaehler_Test {
  public:
	static void api_parse_exception(vz::api::Volkszaehler &v, vz::api::CURLresponse &r, char *&err,
									size_t &n) {
		v.api_parse_exception(r, err, n);
	}
	// the batch prepared by the last api_json_tuples() call
	static const std::vector<Buffer::Entry> &values(Volkszaehler &v) { return v._reader.batch(); }
	static json_object *api_json_tuples(Volkszaehler &v) { return v.api_json_tuples(); };
	// simulate a successful request: acknowledge the batch
	static void ack(Volkszaehler &v) { v._reader.commit(); }
	static void ack_first(Volkszaehler &v) { v._reader.commit_first(); }
};
} // namespace api
} // namespace vz

Config_Options options(config_file());

namespace {
// a reading becomes visible to the API once the reading thread published it
void push(Channel *ch, const Reading &r) {
	ch->push(r);
	ch->buffer()->publish(0, false);
}
::testing::AssertionResult same(const Buffer::Entry &e, const Reading &r) {
	if (e.time_ms == r.time_ms() && e.value == r.value())
		return ::testing::AssertionSuccess();
	return ::testing::AssertionFailure()
		   << "entry (" << e.time_ms << ", " << e.value << ") != reading (" << r.time_ms() << ", "
		   << r.value() << ")";
}
} // namespace

#ifdef HAVE_CPP_REGEX

TEST(api_Volkszaehler, regex_for_configs) {
	// ^\s*(#|$)
	std::regex regex("^\\s*(//(.*|)|$)");
	EXPECT_TRUE(std::regex_match("", regex));
	EXPECT_TRUE(std::regex_match("//", regex));
	EXPECT_TRUE(std::regex_match("//bla", regex));
	EXPECT_TRUE(std::regex_match("// bla", regex));
	EXPECT_TRUE(std::regex_match("\t//", regex));
	EXPECT_TRUE(std::regex_match("\t// bla", regex));
	EXPECT_TRUE(std::regex_match("  //", regex));
	// some neg. tests:
	EXPECT_FALSE(std::regex_match("bla", regex));
}

#endif

TEST(api_Volkszaehler, config_options) {
	MapContainer mappings;
#ifdef OCR_SUPPORT
	options.config_parse(mappings); // let's see whether we can parse the provided example config
#else
	// doesn't throw anylonger since OCR removed by basic example conf.
	// ASSERT_THROW(options.config_parse(mappings), vz::VZException); // the default config should
	// fail due to missing Protocol ocr. There might be another failure... so parse it anyhow here:
	options.config_parse(mappings);
#endif
}

TEST(api_Volkszaehler, no_middleware) {
	using namespace vz::api;
	std::list<Option> options;
	// options.push_front(Option("middleware", (char*)"bla_middleware"));
	ReadingIdentifier::Ptr pRid;
	Channel *ch = new Channel(options, std::string("bla_api"), std::string("bla_uuid"), pRid);
	Channel::Ptr chp(ch);
	ASSERT_THROW(Volkszaehler v(chp, options), vz::VZException);
}

TEST(api_Volkszaehler, api_parse_exception) {
	using namespace vz::api;
	CURLresponse resp;
	resp.data = 0;
	resp.size = 0;
	char *err = new char[100];
	err[0] = 0;
	size_t n = 100;
	std::list<Option> options;
	options.push_front(Option("middleware", (char *)"bla_middleware"));
	ReadingIdentifier::Ptr pRid;
	Channel *ch = new Channel(options, std::string("bla_api"), std::string("bla_uuid"), pRid);
	Channel::Ptr chp(ch);
	Volkszaehler v(chp, options);

	// test using empty data:
	// todo bug: this writes in err even if its null!
	Volkszaehler_Test::api_parse_exception(v, resp, err, n);
	ASSERT_STREQ("continue", err) << err;

	// test using small size for n:
	n = 5;
	Volkszaehler_Test::api_parse_exception(v, resp, err, n);
	// todo bug: doesn't zero terminate the string! ASSERT_STREQ("conti", err ) << err;
	n = 100;

	// test using corrupted resp data:
	resp.size = 100;
	resp.data = 0;
	// todo: this crashes! Volkszaehler_Test::api_parse_exception(v, resp, err, n);

	// test using empty json resp:
	resp.data = (char *)"{}%";
	resp.size = strlen(resp.data);
	Volkszaehler_Test::api_parse_exception(v, resp, err, n);
	ASSERT_STREQ("Missing exception", err);

	// test using just exception:
	resp.data = (char *)"{exception}%";
	resp.size = strlen(resp.data);
	Volkszaehler_Test::api_parse_exception(v, resp, err, n);
	ASSERT_STREQ("quoted object property name expected", err);

	// test using just "exception":
	resp.data = (char *)"{\"exception\"}%";
	resp.size = strlen(resp.data);
	Volkszaehler_Test::api_parse_exception(v, resp, err, n);
	ASSERT_STREQ("object property name separator ':' expected", err);

	// test using proper "exception":
	resp.data = (char *)"{\"exception\":\"1\"}%";
	resp.size = strlen(resp.data);
	// todo bug throws a c++ exception (null not valid) Volkszaehler_Test::api_parse_exception(v,
	// resp, err, n); ASSERT_STREQ("object property name separator ':' expected", err);

	// test using proper "exception" but not type/message:
	resp.data = (char *)"{\"exception\": { \"type\":\"1\", \"message\":\"2\"  } }%";
	resp.size = strlen(resp.data);
	Volkszaehler_Test::api_parse_exception(v, resp, err, n);
	ASSERT_STREQ("'1': '2'", err);

	// test type: "UniqueConstraintViolationException" message contains "Duplicate entry"
	resp.data = (char *)"{\"exception\": { \"type\":\"UniqueConstraintViolationException\", "
						"\"message\":\"2 Duplicate entry\"  } }%";
	resp.size = strlen(resp.data);
	// nothing sent yet: nothing to remove
	Volkszaehler_Test::api_parse_exception(v, resp, err, n);
	ASSERT_STREQ("'UniqueConstraintViolationException': '2 Duplicate entry'", err);
	// the first reading of the batch is removed, the others are kept for the next request
	struct timeval t;
	t.tv_usec = 0;
	t.tv_sec = 1;
	push(ch, Reading(1.0, t, pRid));
	t.tv_sec = 2;
	push(ch, Reading(2.0, t, pRid));
	json_object *j = Volkszaehler_Test::api_json_tuples(v);
	ASSERT_EQ(2u, Volkszaehler_Test::values(v).size());
	json_object_put(j);
	Volkszaehler_Test::api_parse_exception(v, resp, err, n);
	ASSERT_STREQ("'UniqueConstraintViolationException': '2 Duplicate entry'", err);
	ASSERT_EQ(1u, ch->buffer()->size());
	j = Volkszaehler_Test::api_json_tuples(v);
	ASSERT_EQ(1u, Volkszaehler_Test::values(v).size());
	ASSERT_EQ(2.0, Volkszaehler_Test::values(v).front().value);
	json_object_put(j);

	delete[] err;
}

TEST(api_Volkszaehler, api_json_tuples_no_duplicates) {
	using namespace vz::api;
	std::list<Option> options;
	options.push_front(Option("middleware", (char *)"bla_middleware"));
	ReadingIdentifier::Ptr pRid;
	Channel *ch = new Channel(options, std::string("bla_api"), std::string("bla_uuid"), pRid);
	Channel::Ptr chp(ch);
	Volkszaehler v(chp, options);

	// test using empty data:
	json_object *j = Volkszaehler_Test::api_json_tuples(v);
	ASSERT_TRUE(j == 0);
	ASSERT_TRUE(Volkszaehler_Test::values(v).size() == 0);

	struct timeval t1;
	t1.tv_sec = 1;
	t1.tv_usec = 1;
	struct timeval t2;
	t2.tv_sec = 2;
	t2.tv_usec = 1;
	Reading r1(1.0, t1, pRid);
	Reading r2(1.0, t2, pRid);
	push(ch, r1);

	// expect one data returned in values:
	j = Volkszaehler_Test::api_json_tuples(v);
	ASSERT_TRUE(j != 0);
	ASSERT_TRUE(Volkszaehler_Test::values(v).size() == 1);
	ASSERT_TRUE(same(Volkszaehler_Test::values(v).front(), r1));
	json_object_put(j);
	// not acknowledged (e.g. request failed): stays in the buffer
	ASSERT_EQ(1u, ch->buffer()->size());

	push(ch, r1);
	push(ch, r2);
	// expect two data returned in values: r1 (not acknowledged yet) and r2. The 2nd r1 is
	// ignored as its timestamp is the same as the previous one.
	j = Volkszaehler_Test::api_json_tuples(v);
	ASSERT_TRUE(j != 0);
	ASSERT_TRUE(Volkszaehler_Test::values(v).size() == 2);
	ASSERT_TRUE(same(Volkszaehler_Test::values(v).front(), r1));
	ASSERT_TRUE(same(Volkszaehler_Test::values(v).back(), r2));

	json_object_put(j);
	Volkszaehler_Test::ack(v);
	ASSERT_TRUE(ch->buffer()->size() == 0);
}

TEST(api_Volkszaehler, api_json_tuples_duplicates) {
	using namespace vz::api;
	std::list<Option> options;
	options.push_front(Option("middleware", (char *)"bla_middleware"));
	options.push_back(Option("duplicates", 10));
	ReadingIdentifier::Ptr pRid;
	Channel *ch = new Channel(options, std::string("bla_api"), std::string("bla_uuid"), pRid);
	Channel::Ptr chp(ch);
	Volkszaehler v(chp, options);

	// test using empty data:
	json_object *j = Volkszaehler_Test::api_json_tuples(v);
	ASSERT_TRUE(j == 0);
	ASSERT_TRUE(Volkszaehler_Test::values(v).size() == 0);

	struct timeval t1;
	t1.tv_sec = 1;
	t1.tv_usec = 1;
	struct timeval t2;
	t2.tv_sec = 2;
	t2.tv_usec = 1;
	Reading r1(1.0, t1, pRid);
	Reading r2(1.0, t2, pRid);
	push(ch, r1);

	// expect one data returned in values:
	j = Volkszaehler_Test::api_json_tuples(v);
	ASSERT_TRUE(j != 0);
	ASSERT_TRUE(Volkszaehler_Test::values(v).size() == 1);
	ASSERT_TRUE(same(Volkszaehler_Test::values(v).front(), r1));
	json_object_put(j);

	push(ch, r1);
	push(ch, r2);
	// expect one data returned in values: (r1 same timestamp, r2 ignored)
	j = Volkszaehler_Test::api_json_tuples(v);
	ASSERT_TRUE(j != 0);
	ASSERT_TRUE(Volkszaehler_Test::values(v).size() == 1);
	ASSERT_TRUE(same(Volkszaehler_Test::values(v).front(), r1));
	json_object_put(j);

	struct timeval t3;
	t3.tv_sec = 2;
	t3.tv_usec = 1 + 1000; // at least one ms distance
	Reading r3(2.0, t3, pRid);
	push(ch, r3);

	// now add one with a different value:
	// then we should get r1 and the new value r3:
	j = Volkszaehler_Test::api_json_tuples(v);
	ASSERT_TRUE(j != 0);
	ASSERT_TRUE(Volkszaehler_Test::values(v).size() == 2);
	ASSERT_TRUE(same(Volkszaehler_Test::values(v).front(), r1));
	ASSERT_TRUE(same(Volkszaehler_Test::values(v).back(), r3));
	json_object_put(j);
	Volkszaehler_Test::ack_first(v); // only r1 is done

	// now try timeout:
	// add r4 with same value but distance >10s
	t1.tv_sec = t3.tv_sec + 10;
	t1.tv_usec = t3.tv_usec;
	Reading r4(2.0, t1, pRid);
	push(ch, r4);
	// now there should be r3 and r4:
	j = Volkszaehler_Test::api_json_tuples(v);
	ASSERT_TRUE(j != 0);
	ASSERT_TRUE(Volkszaehler_Test::values(v).size() == 2) << Volkszaehler_Test::values(v).size();
	ASSERT_TRUE(same(Volkszaehler_Test::values(v).front(), r3));
	ASSERT_TRUE(same(Volkszaehler_Test::values(v).back(), r4));
	json_object_put(j);
	Volkszaehler_Test::ack(v);
	ASSERT_TRUE(ch->buffer()->size() == 0);

	// now try timeout and value change:
	// expect only new value to be added:
	t1.tv_sec += 11;
	Reading r5(5.0, t1, pRid);
	push(ch, r5);
	j = Volkszaehler_Test::api_json_tuples(v);
	ASSERT_TRUE(j != 0);
	ASSERT_TRUE(Volkszaehler_Test::values(v).size() == 1) << Volkszaehler_Test::values(v).size();
	ASSERT_TRUE(same(Volkszaehler_Test::values(v).front(), r5));
	json_object_put(j);
	Volkszaehler_Test::ack(v);
	ASSERT_TRUE(ch->buffer()->size() == 0);

	// now ignore one
	t1.tv_sec += 1;
	Reading r6(5.0, t1, pRid);
	push(ch, r6);
	j = Volkszaehler_Test::api_json_tuples(v);
	ASSERT_TRUE(j == 0);
	ASSERT_TRUE(Volkszaehler_Test::values(v).size() == 0) << Volkszaehler_Test::values(v).size();
	// ignored readings are acknowledged right away
	ASSERT_TRUE(ch->buffer()->size() == 0);

	// and try timeout and value change again
	// expect new value to be added:

	t1.tv_sec += 10;
	Reading r7(7.0, t1, pRid);
	push(ch, r7);
	j = Volkszaehler_Test::api_json_tuples(v);
	ASSERT_TRUE(j != 0);
	ASSERT_TRUE(Volkszaehler_Test::values(v).size() == 1) << Volkszaehler_Test::values(v).size();
	ASSERT_TRUE(same(Volkszaehler_Test::values(v).front(), r7));
	json_object_put(j);
	Volkszaehler_Test::ack(v);
	ASSERT_TRUE(ch->buffer()->size() == 0);
}
