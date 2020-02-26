#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <utility>

#include "src/stirling/http/http_parse.h"

namespace pl {
namespace stirling {
namespace http {

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Key;
using ::testing::Not;
using ::testing::Pair;

TEST(PreProcessRecordTest, GzipCompressedContentIsDecompressed) {
  Message message;
  message.http_headers.insert({kContentEncoding, "gzip"});
  const uint8_t compressed_bytes[] = {0x1f, 0x8b, 0x08, 0x00, 0x37, 0xf0, 0xbf, 0x5c, 0x00,
                                      0x03, 0x0b, 0xc9, 0xc8, 0x2c, 0x56, 0x00, 0xa2, 0x44,
                                      0x85, 0x92, 0xd4, 0xe2, 0x12, 0x2e, 0x00, 0x8c, 0x2d,
                                      0xc0, 0xfa, 0x0f, 0x00, 0x00, 0x00};
  message.http_msg_body.assign(reinterpret_cast<const char*>(compressed_bytes),
                               sizeof(compressed_bytes));
  PreProcessMessage(&message);
  EXPECT_EQ("This is a test\n", message.http_msg_body);
}

TEST(PreProcessRecordTest, ContentHeaderIsNotAdded) {
  Message message;
  message.http_msg_body = "test";
  PreProcessMessage(&message);
  EXPECT_EQ("test", message.http_msg_body);
  EXPECT_THAT(message.http_headers, Not(Contains(Key(kContentEncoding))));
}

TEST(ParseHTTPHeaderFiltersAndMatchTest, FiltersAreAsExpectedAndMatchesWork) {
  const std::string filters_string =
      "Content-Type:json,,,,Content-Type:plain,Transfer-Encoding:chunked,,,Transfer-Encoding:"
      "chunked,-Content-Encoding:gzip,-Content-Encoding:binary";
  HTTPHeaderFilter filter = ParseHTTPHeaderFilters(filters_string);
  EXPECT_THAT(filter.inclusions,
              ElementsAre(Pair("Content-Type", "json"), Pair("Content-Type", "plain"),
                          Pair("Transfer-Encoding", "chunked"),
                          // Note that multimap does not remove duplicates.
                          Pair("Transfer-Encoding", "chunked")));
  EXPECT_THAT(filter.exclusions,
              ElementsAre(Pair("Content-Encoding", "gzip"), Pair("Content-Encoding", "binary")));
  {
    HeadersMap http_headers = {
        {"Content-Type", "application/json; charset=utf-8"},
    };
    EXPECT_TRUE(MatchesHTTPTHeaders(http_headers, filter));
    http_headers.insert({"Content-Encoding", "gzip"});
    EXPECT_FALSE(MatchesHTTPTHeaders(http_headers, filter)) << "gzip should be filtered out";
  }
  {
    HeadersMap http_headers = {
        {"Transfer-Encoding", "chunked"},
    };
    EXPECT_TRUE(MatchesHTTPTHeaders(http_headers, filter));
    http_headers.insert({"Content-Encoding", "binary"});
    EXPECT_FALSE(MatchesHTTPTHeaders(http_headers, filter)) << "binary should be filtered out";
  }
  {
    HeadersMap http_headers;
    EXPECT_FALSE(MatchesHTTPTHeaders(http_headers, filter));

    const HTTPHeaderFilter empty_filter;
    EXPECT_TRUE(MatchesHTTPTHeaders(http_headers, empty_filter))
        << "Empty filter matches any HTTP headers";
    http_headers.insert({"Content-Type", "non-matching-type"});
    EXPECT_TRUE(MatchesHTTPTHeaders(http_headers, empty_filter))
        << "Empty filter matches any HTTP headers";
  }
  {
    const HeadersMap http_headers = {
        {"Content-Type", "non-matching-type"},
    };
    EXPECT_FALSE(MatchesHTTPTHeaders(http_headers, filter));
  }
}

TEST(HTTPHeadersMap, CaseInsensitivity) {
  const HeadersMap kHTTPHeaders = {
      {"Content-Type", "application/json"},
      {"Transfer-Encoding", "chunked"},
  };

  // Check that we can access the key regardless of the case of the key (content-type).
  {
    auto http_headers_iter = kHTTPHeaders.find("content-type");
    ASSERT_NE(http_headers_iter, kHTTPHeaders.end());
    EXPECT_EQ(http_headers_iter->second, "application/json");
  }

  {
    auto http_headers_iter = kHTTPHeaders.find("Content-Type");
    ASSERT_NE(http_headers_iter, kHTTPHeaders.end());
    EXPECT_EQ(http_headers_iter->second, "application/json");
  }
}

//=============================================================================
// Test Utilities
//=============================================================================

const std::string_view kHTTPGetReq0 =
    "GET /index.html HTTP/1.1\r\n"
    "Host: www.pixielabs.ai\r\n"
    "Accept: image/gif, image/jpeg, */*\r\n"
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64)\r\n"
    "\r\n";

Message HTTPGetReq0ExpectedMessage() {
  Message expected_message;
  expected_message.type = MessageType::kRequest;
  expected_message.http_minor_version = 1;
  expected_message.http_headers = {{"Host", "www.pixielabs.ai"},
                                   {"Accept", "image/gif, image/jpeg, */*"},
                                   {"User-Agent", "Mozilla/5.0 (X11; Linux x86_64)"}};
  expected_message.http_req_method = "GET";
  expected_message.http_req_path = "/index.html";
  expected_message.http_msg_body = "-";
  return expected_message;
}

const std::string_view kHTTPGetReq1 =
    "GET /foo.html HTTP/1.1\r\n"
    "Host: www.pixielabs.ai\r\n"
    "Accept: image/gif, image/jpeg, */*\r\n"
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64)\r\n"
    "\r\n";

Message HTTPGetReq1ExpectedMessage() {
  Message expected_message;
  expected_message.type = MessageType::kRequest;
  expected_message.http_minor_version = 1;
  expected_message.http_headers = {{"Host", "www.pixielabs.ai"},
                                   {"Accept", "image/gif, image/jpeg, */*"},
                                   {"User-Agent", "Mozilla/5.0 (X11; Linux x86_64)"}};
  expected_message.http_req_method = "GET";
  expected_message.http_req_path = "/foo.html";
  expected_message.http_msg_body = "-";
  return expected_message;
}

const std::string_view kHTTPPostReq0 =
    "POST /test HTTP/1.1\r\n"
    "host: pixielabs.ai\r\n"
    "content-type: application/x-www-form-urlencoded\r\n"
    "content-length: 27\r\n"
    "\r\n"
    "field1=value1&field2=value2";

Message HTTPPostReq0ExpectedMessage() {
  Message expected_message;
  expected_message.type = MessageType::kRequest;
  expected_message.http_minor_version = 1;
  expected_message.http_headers = {{"host", "pixielabs.ai"},
                                   {"content-type", "application/x-www-form-urlencoded"},
                                   {"content-length", "27"}};
  expected_message.http_req_method = "POST";
  expected_message.http_req_path = "/test";
  expected_message.http_msg_body = "field1=value1&field2=value2";
  return expected_message;
}

const std::string_view kHTTPResp0 =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: foo\r\n"
    "Content-Length: 9\r\n"
    "\r\n"
    "pixielabs";

Message HTTPResp0ExpectedMessage() {
  Message expected_message;
  expected_message.type = MessageType::kResponse;
  expected_message.http_minor_version = 1;
  expected_message.http_headers = {{"Content-Type", "foo"}, {"Content-Length", "9"}};
  expected_message.http_resp_status = 200;
  expected_message.http_resp_message = "OK";
  expected_message.http_msg_body = "pixielabs";
  return expected_message;
}

const std::string_view kHTTPResp1 =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: bar\r\n"
    "Content-Length: 21\r\n"
    "\r\n"
    "pixielabs is awesome!";

Message HTTPResp1ExpectedMessage() {
  Message expected_message;
  expected_message.type = MessageType::kResponse;
  expected_message.http_minor_version = 1;
  expected_message.http_headers = {{"Content-Type", "bar"}, {"Content-Length", "21"}};
  expected_message.http_resp_status = 200;
  expected_message.http_resp_message = "OK";
  expected_message.http_msg_body = "pixielabs is awesome!";
  return expected_message;
}

const std::string_view kHTTPResp2 =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"
    "9\r\n"
    "pixielabs\r\n"
    "C\r\n"
    " is awesome!\r\n"
    "0\r\n"
    "\r\n";

Message HTTPResp2ExpectedMessage() {
  Message expected_message;
  expected_message.type = MessageType::kResponse;
  expected_message.http_minor_version = 1;
  expected_message.http_headers = {{"Transfer-Encoding", "chunked"}};
  expected_message.http_resp_status = 200;
  expected_message.http_resp_message = "OK";
  expected_message.http_msg_body = "pixielabs is awesome!";
  return expected_message;
}

Message EmptyHTTPResp() {
  Message result;
  result.type = MessageType::kResponse;
  result.http_minor_version = 1;
  result.http_resp_status = 200;
  result.http_resp_message = "OK";
  return result;
}

// Leave http_msg_body set by caller.
Message EmptyChunkedHTTPResp() {
  Message result = EmptyHTTPResp();
  result.http_headers = {{"Transfer-Encoding", "chunked"}};
  return result;
}

MATCHER_P(HasBody, body, "") { return arg.http_msg_body == body; }

std::string HTTPRespWithSizedBody(std::string_view body) {
  return absl::Substitute(
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: $0\r\n"
      "\r\n"
      "$1",
      body.size(), body);
}

std::string HTTPChunk(std::string_view chunk_body) {
  const char size = chunk_body.size() < 10 ? '0' + chunk_body.size() : 'A' + chunk_body.size() - 10;
  std::string s(1, size);
  return absl::StrCat(s, "\r\n", chunk_body, "\r\n");
}

std::string HTTPRespWithChunkedBody(std::vector<std::string_view> chunk_bodies) {
  std::string result =
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n";
  for (auto c : chunk_bodies) {
    absl::StrAppend(&result, HTTPChunk(c));
  }
  // Lastly, append a 0-length chunk.
  absl::StrAppend(&result, HTTPChunk(""));
  return result;
}

bool operator==(const Message& lhs, const Message& rhs) {
#define CMP(field)                                                 \
  if (lhs.field != rhs.field) {                                    \
    LOG(INFO) << #field ": " << lhs.field << " vs. " << rhs.field; \
    return false;                                                  \
  }
  CMP(http_minor_version);
  CMP(http_resp_status);
  CMP(http_resp_message);
  if (lhs.http_headers != rhs.http_headers) {
    LOG(INFO) << absl::StrJoin(std::begin(lhs.http_headers), std::end(lhs.http_headers), " ",
                               absl::PairFormatter(":"))
              << " vs. "
              << absl::StrJoin(std::begin(rhs.http_headers), std::end(rhs.http_headers), " ",
                               absl::PairFormatter(":"));
    return false;
  }
  if (lhs.type != rhs.type) {
    LOG(INFO) << static_cast<int>(lhs.type) << " vs. " << static_cast<int>(rhs.type);
  }
  return true;
}

// Utility function that takes a string view of a buffer, and a set of N split points,
// and returns a set of N+1 split string_views of the buffer.
std::vector<std::string_view> MessageSplit(std::string_view msg, std::vector<size_t> split_points) {
  std::vector<std::string_view> splits;

  split_points.push_back(msg.length());
  std::sort(split_points.begin(), split_points.end());

  // Check for bad split_points.
  CHECK_EQ(msg.length(), split_points.back());

  uint32_t curr_pos = 0;
  for (auto split_point : split_points) {
    auto split = std::string_view(msg).substr(curr_pos, split_point - curr_pos);
    splits.push_back(split);
    curr_pos = split_point;
  }

  return splits;
}

//=============================================================================
// HTTP Parse() Tests
//=============================================================================

TEST(ParseTest, CompleteMessages) {
  std::string msg_a = HTTPRespWithSizedBody("a");
  std::string msg_b = HTTPRespWithChunkedBody({"b"});
  std::string msg_c = HTTPRespWithSizedBody("c");
  std::string buf = absl::StrCat(msg_a, msg_b, msg_c);

  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kResponse, buf, &parsed_messages);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_EQ(msg_a.size() + msg_b.size() + msg_c.size(), result.end_position);
  EXPECT_THAT(parsed_messages, ElementsAre(HasBody("a"), HasBody("b"), HasBody("c")));
  EXPECT_THAT(result.start_positions, ElementsAre(0, msg_a.size(), msg_a.size() + msg_b.size()));
}

TEST(ParseTest, PartialHeader) {
  // Partial header: Content-type value is missing, and no final \r\n.
  std::string msg =
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 40\r\n"
      "Content-Type:";

  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kResponse, msg, &parsed_messages);

  EXPECT_EQ(ParseState::kNeedsMoreData, result.state);
  EXPECT_EQ(0, result.end_position);
  EXPECT_THAT(parsed_messages, IsEmpty());
}

TEST(ParseTest, PartialBody) {
  // Headers are complete but body is not 40 bytes, indicating a partial body.
  std::string msg =
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 40\r\n"
      "\r\n"
      "Foo";

  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kResponse, msg, &parsed_messages);

  EXPECT_EQ(ParseState::kNeedsMoreData, result.state);
  EXPECT_EQ(0, result.end_position);
  EXPECT_THAT(parsed_messages, IsEmpty());
}

TEST(ParseTest, Status101) {
  std::string switch_protocol_msg =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "\r\n";
  std::string new_protocol_data = "New protocol data";

  std::string data = absl::StrCat(switch_protocol_msg, new_protocol_data);

  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kResponse, data, &parsed_messages);

  EXPECT_EQ(ParseState::kEOS, result.state);
  EXPECT_EQ(switch_protocol_msg.size(), result.end_position);
  EXPECT_THAT(parsed_messages, ElementsAre(HasBody("")));
}

TEST(ParseTest, Status204) {
  std::string msg =
      "HTTP/1.1 204 No Content\r\n"
      "\r\n";

  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kResponse, msg, &parsed_messages);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_THAT(parsed_messages, ElementsAre(HasBody("")));
}

//=============================================================================
// HTTP Parsing Tests
//=============================================================================

// Parameter used for stress/fuzz test.
struct TestParam {
  uint32_t seed;
  uint32_t iters;
};

std::vector<SocketDataEvent> CreateEvents(const std::vector<std::string_view>& msgs) {
  std::vector<SocketDataEvent> events;
  for (size_t i = 0; i < msgs.size(); ++i) {
    events.emplace_back();
    events.back().msg = msgs[i];
  }
  return events;
}

class HTTPParserTest : public ::testing::TestWithParam<TestParam> {
 protected:
  void ParserAppendEvents(const std::vector<SocketDataEvent>& events) {
    for (const auto& event : events) {
      parser_.Append(event);
    }
  }

  EventParser<Message> parser_;
};

TEST_F(HTTPParserTest, ParseCompleteHTTPResponseWithContentLengthHeader) {
  std::string_view msg1 =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: foo\r\n"
      "Content-Length: 9\r\n"
      "\r\n"
      "pixielabs";

  std::string_view msg2 =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: bar\r\n"
      "Content-Length: 10\r\n"
      "\r\n"
      "pixielabs!";

  Message expected_message1;
  expected_message1.type = MessageType::kResponse;
  expected_message1.http_minor_version = 1;
  expected_message1.http_headers = {{"Content-Type", "foo"}, {"Content-Length", "9"}};
  expected_message1.http_resp_status = 200;
  expected_message1.http_resp_message = "OK";
  expected_message1.http_msg_body = "pixielabs";

  Message expected_message2;
  expected_message2.type = MessageType::kResponse;
  expected_message2.http_minor_version = 1;
  expected_message2.http_headers = {{"Content-Type", "bar"}, {"Content-Length", "10"}};
  expected_message2.http_resp_status = 200;
  expected_message2.http_resp_message = "OK";
  expected_message2.http_msg_body = "pixielabs!";

  std::deque<Message> parsed_messages;
  const std::string buf = absl::StrCat(msg1, msg2);
  ParseResult result = ParseFrame(MessageType::kResponse, buf, &parsed_messages);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_THAT(parsed_messages, ElementsAre(expected_message1, expected_message2));
}

TEST_F(HTTPParserTest, ParseIncompleteHTTPResponseWithContentLengthHeader) {
  const std::string_view msg1 =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: foo\r\n"
      "Content-Length: 21\r\n"
      "\r\n"
      "pixielabs";
  const std::string_view msg2 = " is awesome";
  const std::string_view msg3 = "!";

  Message expected_message1;
  expected_message1.type = MessageType::kResponse;
  expected_message1.http_minor_version = 1;
  expected_message1.http_headers = {{"Content-Type", "foo"}, {"Content-Length", "21"}};
  expected_message1.http_resp_status = 200;
  expected_message1.http_resp_message = "OK";
  expected_message1.http_msg_body = "pixielabs is awesome!";

  const std::string buf = absl::StrCat(msg1, msg2, msg3);
  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kResponse, buf, &parsed_messages);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_THAT(parsed_messages, ElementsAre(expected_message1));
}

TEST_F(HTTPParserTest, InvalidInput) {
  const std::string_view buf = " is awesome";
  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kResponse, buf, &parsed_messages);

  EXPECT_EQ(ParseState::kInvalid, result.state);
  EXPECT_THAT(parsed_messages, ElementsAre());
}

TEST_F(HTTPParserTest, NoAppend) {
  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kResponse, "", &parsed_messages);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_THAT(parsed_messages, ElementsAre());
}

TEST_F(HTTPParserTest, ParseCompleteChunkEncodedMessage) {
  std::string msg =
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "9\r\n"
      "pixielabs\r\n"
      "C\r\n"
      " is awesome!\r\n"
      "0\r\n";
  Message expected_message = EmptyChunkedHTTPResp();
  expected_message.http_msg_body = "pixielabs is awesome!";

  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kResponse, msg, &parsed_messages);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_THAT(parsed_messages, ElementsAre(expected_message));
}

TEST_F(HTTPParserTest, ParseMultipleChunks) {
  std::string msg1 =
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "9\r\n"
      "pixielabs\r\n";
  std::string msg2 = "C\r\n is awesome!\r\n";
  std::string msg3 = "0\r\n\r\n";

  Message expected_message = EmptyChunkedHTTPResp();
  expected_message.http_msg_body = "pixielabs is awesome!";

  const std::string buf = absl::StrCat(msg1, msg2, msg3);
  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kResponse, buf, &parsed_messages);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_THAT(parsed_messages, ElementsAre(expected_message));
}

TEST_F(HTTPParserTest, ParseIncompleteChunks) {
  std::string msg1 =
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "9\r\n"
      "pixie";
  std::string msg2 = "labs\r\n";
  std::string msg3 = "0\r\n\r\n";

  Message expected_message = EmptyChunkedHTTPResp();
  expected_message.http_msg_body = "pixielabs";

  const std::string buf = absl::StrCat(msg1, msg2, msg3);
  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kResponse, buf, &parsed_messages);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_THAT(parsed_messages, ElementsAre(expected_message));
}

// Note that many other tests already use requests with no content-length,
// but keeping this explicitly here in case the other tests change.
TEST_F(HTTPParserTest, ParseRequestWithoutLengthOrChunking) {
  std::string msg1 =
      "HEAD /foo.html HTTP/1.1\r\n"
      "Host: www.pixielabs.ai\r\n"
      "Accept: image/gif, image/jpeg, */*\r\n"
      "User-Agent: Mozilla/5.0 (X11; Linux x86_64)\r\n"
      "\r\n";

  Message expected_message;
  expected_message.type = MessageType::kRequest;
  expected_message.http_minor_version = 1;
  expected_message.http_headers = {{"Host", "www.pixielabs.ai"},
                                   {"Accept", "image/gif, image/jpeg, */*"},
                                   {"User-Agent", "Mozilla/5.0 (X11; Linux x86_64)"}};
  expected_message.http_req_method = "GET";
  expected_message.http_req_path = "/index.html";
  expected_message.http_msg_body = "-";

  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kRequest, msg1, &parsed_messages);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_THAT(parsed_messages, ElementsAre(expected_message));
}

// When a response has no content-length or transfer-encoding,
// and it is not one of a set of known status codes with known bodies,
// then we capture as much data as is available at the time.
TEST_F(HTTPParserTest, ParseResponseWithoutLengthOrChunking) {
  std::string msg1 =
      "HTTP/1.1 200 OK\r\n"
      "\r\n"
      "pixielabs is aweso";

  Message expected_message = EmptyHTTPResp();
  expected_message.http_msg_body = "pixielabs is aweso";

  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kResponse, msg1, &parsed_messages);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_THAT(parsed_messages, ElementsAre(expected_message));
}

TEST_F(HTTPParserTest, MessagePartialHeaders) {
  std::string msg1 =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain";

  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kResponse, msg1, &parsed_messages);

  EXPECT_EQ(ParseState::kNeedsMoreData, result.state);
  EXPECT_THAT(parsed_messages, ElementsAre());
}

TEST_F(HTTPParserTest, PartialMessageInTheMiddleOfStream) {
  std::string msg0 = HTTPRespWithSizedBody("foobar") + "HTTP/1.1 200 OK\r\n";
  std::string msg1 = "Transfer-Encoding: chunked\r\n\r\n";
  std::string msg2 = HTTPChunk("pixielabs ");
  std::string msg3 = HTTPChunk("rocks!");
  std::string msg4 = HTTPChunk("");

  const std::string buf = absl::StrCat(msg0, msg1, msg2, msg3, msg4);
  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kResponse, buf, &parsed_messages);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_THAT(parsed_messages, ElementsAre(HasBody("foobar"), HasBody("pixielabs rocks!")));
}

//=============================================================================
// HTTP Parsing Stress Tests
//=============================================================================

TEST_F(HTTPParserTest, ParseHTTPRequestSingle) {
  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kRequest, kHTTPGetReq0, &parsed_messages);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_THAT(parsed_messages, ElementsAre(HTTPGetReq0ExpectedMessage()));
}

TEST_F(HTTPParserTest, ParseHTTPRequestMultiple) {
  const std::string buf = absl::StrCat(kHTTPGetReq0, kHTTPPostReq0);
  std::deque<Message> parsed_messages;
  ParseResult result = ParseFrame(MessageType::kRequest, buf, &parsed_messages);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_THAT(parsed_messages,
              ElementsAre(HTTPGetReq0ExpectedMessage(), HTTPPostReq0ExpectedMessage()));
}

TEST_P(HTTPParserTest, ParseHTTPRequestsRepeatedly) {
  std::string msg = absl::StrCat(kHTTPGetReq0, kHTTPPostReq0);

  std::default_random_engine rng;
  rng.seed(GetParam().seed);
  std::uniform_int_distribution<uint32_t> splitpoint_dist(0, msg.length());

  for (uint32_t i = 0; i < GetParam().iters; ++i) {
    // Choose two random split points for this iteration.
    std::vector<size_t> split_points;
    split_points.push_back(splitpoint_dist(rng));
    split_points.push_back(splitpoint_dist(rng));

    std::vector<std::string_view> msg_splits = MessageSplit(msg, split_points);
    ASSERT_EQ(msg, absl::StrCat(msg_splits[0], msg_splits[1], msg_splits[2]));

    SocketDataEvent event0;
    event0.msg = msg_splits[0];
    SocketDataEvent event1;
    event1.msg = msg_splits[1];
    SocketDataEvent event2;
    event2.msg = msg_splits[2];

    parser_.Append(event0);
    parser_.Append(event1);
    parser_.Append(event2);

    std::deque<Message> parsed_messages;
    ParseResult result = parser_.ParseMessages(MessageType::kRequest, &parsed_messages);

    ASSERT_EQ(ParseState::kSuccess, result.state);
    ASSERT_THAT(parsed_messages,
                ElementsAre(HTTPGetReq0ExpectedMessage(), HTTPPostReq0ExpectedMessage()));
  }
}

TEST_P(HTTPParserTest, ParseHTTPResponsesRepeatedly) {
  std::string msg = absl::StrCat(kHTTPResp0, kHTTPResp1, kHTTPResp2);

  std::default_random_engine rng;
  rng.seed(GetParam().seed);
  std::uniform_int_distribution<size_t> splitpoint_dist(0, msg.length());

  for (uint32_t i = 0; i < GetParam().iters; ++i) {
    // Choose two random split points for this iteration.
    std::vector<size_t> split_points;
    split_points.push_back(splitpoint_dist(rng));
    split_points.push_back(splitpoint_dist(rng));

    std::vector<std::string_view> msg_splits = MessageSplit(msg, split_points);

    ASSERT_EQ(msg, absl::StrCat(msg_splits[0], msg_splits[1], msg_splits[2]));

    SocketDataEvent event0;
    event0.msg = msg_splits[0];
    SocketDataEvent event1;
    event1.msg = msg_splits[1];
    SocketDataEvent event2;
    event2.msg = msg_splits[2];

    parser_.Append(event0);
    parser_.Append(event1);
    parser_.Append(event2);

    std::deque<Message> parsed_messages;
    ParseResult result = parser_.ParseMessages(MessageType::kResponse, &parsed_messages);

    ASSERT_EQ(ParseState::kSuccess, result.state);
    ASSERT_THAT(parsed_messages, ElementsAre(HTTPResp0ExpectedMessage(), HTTPResp1ExpectedMessage(),
                                             HTTPResp2ExpectedMessage()));
  }
}

// Tests the case where the ParseMessages results in some leftover unprocessed data
// that needs to be processed after more data is added to the buffer.
// ParseHTTPResponseWithLeftoverRepeatedly expands on this by repeating this process many times.
// Keeping this test as a basic filter (easier for debug).
TEST_F(HTTPParserTest, ParseHTTPResponsesWithLeftover) {
  std::string msg = absl::StrCat(kHTTPResp0, kHTTPResp1, kHTTPResp2);

  std::vector<size_t> split_points;
  split_points.push_back(kHTTPResp0.length() - 5);
  split_points.push_back(msg.size() - 10);
  std::vector<std::string_view> msg_splits = MessageSplit(msg, split_points);

  ASSERT_EQ(msg, absl::StrCat(msg_splits[0], msg_splits[1], msg_splits[2]));

  std::vector<SocketDataEvent> events = CreateEvents(msg_splits);

  parser_.Append(events[0]);
  parser_.Append(events[1]);
  // Don't append last split, yet.

  std::deque<Message> parsed_messages;
  ParseResult result = parser_.ParseMessages(MessageType::kResponse, &parsed_messages);

  ASSERT_EQ(ParseState::kNeedsMoreData, result.state);
  ASSERT_THAT(parsed_messages, ElementsAre(HTTPResp0ExpectedMessage(), HTTPResp1ExpectedMessage()));

  BufferPosition position = result.end_position;
  // This replicates the logic inside DataStream::AppendEvents(). We probably should move this test
  // to test DataStream.
  events[position.seq_num].msg.erase(0, position.offset);

  // Now append the unprocessed remainder, including msg_splits[2].
  for (size_t i = position.seq_num; i < events.size(); ++i) {
    parser_.Append(events[i]);
  }

  result = parser_.ParseMessages(MessageType::kResponse, &parsed_messages);

  ASSERT_EQ(ParseState::kSuccess, result.state);
  ASSERT_THAT(parsed_messages, ElementsAre(HTTPResp0ExpectedMessage(), HTTPResp1ExpectedMessage(),
                                           HTTPResp2ExpectedMessage()));
}

// Like ParseHTTPResponsesWithLeftover, but repeats test many times,
// each time with different random split points to stress the functionality.
TEST_P(HTTPParserTest, ParseHTTPResponsesWithLeftoverRepeatedly) {
  std::string msg = absl::StrCat(kHTTPResp0, kHTTPResp1, kHTTPResp2, kHTTPResp1);

  std::default_random_engine rng;
  rng.seed(GetParam().seed);
  std::uniform_int_distribution<size_t> splitpoint_dist(0, msg.length());

  for (uint32_t j = 0; j < GetParam().iters; ++j) {
    // Choose two random split points for this iteration.
    std::vector<size_t> split_points;
    split_points.push_back(splitpoint_dist(rng));
    split_points.push_back(splitpoint_dist(rng));
    std::vector<std::string_view> msg_splits = MessageSplit(msg, split_points);

    ASSERT_EQ(msg, absl::StrCat(msg_splits[0], msg_splits[1], msg_splits[2]));

    std::vector<SocketDataEvent> events = CreateEvents(msg_splits);

    // Append and parse some--but not all--splits.
    parser_.Append(events[0]);
    parser_.Append(events[1]);

    std::deque<Message> parsed_messages;
    ParseResult result1 = parser_.ParseMessages(MessageType::kResponse, &parsed_messages);

    // Now append the unprocessed remainder, including msg_splits[2].
    BufferPosition position = result1.end_position;
    // This replicates the logic inside DataStream::AppendEvents(). We probably should move this
    // test to test DataStream.
    events[position.seq_num].msg.erase(0, position.offset);

    for (size_t i = position.seq_num; i < events.size(); ++i) {
      parser_.Append(events[i]);
    }
    ParseResult result2 = parser_.ParseMessages(MessageType::kResponse, &parsed_messages);

    ASSERT_EQ(ParseState::kSuccess, result2.state);
    ASSERT_THAT(parsed_messages,
                ElementsAre(HTTPResp0ExpectedMessage(), HTTPResp1ExpectedMessage(),
                            HTTPResp2ExpectedMessage(), HTTPResp1ExpectedMessage()));
  }
}

INSTANTIATE_TEST_SUITE_P(Stressor, HTTPParserTest,
                         ::testing::Values(TestParam{37337, 50}, TestParam{98237, 50}));

//=============================================================================
// HTTP FindFrameBoundary Tests
//=============================================================================

TEST_F(HTTPParserTest, FindReqBoundaryAligned) {
  const std::string buf = absl::StrCat(kHTTPGetReq0, kHTTPPostReq0, kHTTPGetReq1);

  size_t pos = FindFrameBoundary<http::Message>(MessageType::kRequest, buf, 0);
  ASSERT_NE(pos, std::string::npos);
  EXPECT_EQ(buf.substr(pos), absl::StrCat(kHTTPGetReq0, kHTTPPostReq0, kHTTPGetReq1));
}

TEST_F(HTTPParserTest, FindRespBoundaryAligned) {
  const std::string buf = absl::StrCat(kHTTPResp0, kHTTPResp1, kHTTPResp2);

  size_t pos = FindFrameBoundary<http::Message>(MessageType::kResponse, buf, 0);
  ASSERT_NE(pos, std::string::npos);
  EXPECT_EQ(buf.substr(pos), absl::StrCat(kHTTPResp0, kHTTPResp1, kHTTPResp2));
}

TEST_F(HTTPParserTest, FindReqBoundaryUnaligned) {
  {
    const std::string buf =
        absl::StrCat("some garbage leftover text with a GET inside", kHTTPPostReq0, kHTTPGetReq1);

    // FindFrameBoundary() should cut out the garbage text and shouldn't match on the GET inside
    // the garbage text.
    size_t pos = FindFrameBoundary<http::Message>(MessageType::kRequest, buf, 0);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(buf.substr(pos), absl::StrCat(kHTTPPostReq0, kHTTPGetReq1));
  }

  {
    const std::string buf =
        absl::StrCat("some garbage leftover text with a POST inside", kHTTPPostReq0, kHTTPGetReq1);

    // FindFrameBoundary() should cut out the garbage text and shouldn't match on the POST inside
    // the garbage text.
    size_t pos = FindFrameBoundary<http::Message>(MessageType::kRequest, buf, 0);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(buf.substr(pos), absl::StrCat(kHTTPPostReq0, kHTTPGetReq1));
  }
}

TEST_F(HTTPParserTest, FindReqBoundaryWithStartPos) {
  const std::string buf = absl::StrCat(kHTTPGetReq0, kHTTPPostReq0, kHTTPGetReq1);

  {
    size_t pos = FindFrameBoundary<http::Message>(MessageType::kRequest, buf, 1);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(buf.substr(pos), absl::StrCat(kHTTPPostReq0, kHTTPGetReq1));
  }

  {
    size_t pos =
        FindFrameBoundary<http::Message>(MessageType::kRequest, buf, kHTTPGetReq0.length() + 1);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(buf.substr(pos), absl::StrCat(kHTTPGetReq1));
  }
}

TEST_F(HTTPParserTest, FindRespBoundaryUnaligned) {
  std::string buf =
      absl::StrCat("some garbage leftover text with a HTTP/1.1 inside", kHTTPResp1, kHTTPResp2);

  // FindFrameBoundary() should cut out the garbage text and shouldn't match on the HTTP/1.1
  // inside the garbage text.
  size_t pos = FindFrameBoundary<http::Message>(MessageType::kResponse, buf, 1);
  ASSERT_NE(pos, std::string::npos);
  EXPECT_EQ(buf.substr(pos), absl::StrCat(kHTTPResp1, kHTTPResp2));
}

TEST_F(HTTPParserTest, FindRespBoundaryWithStartPos) {
  const std::string buf = absl::StrCat(kHTTPResp0, kHTTPResp1, kHTTPResp2);

  {
    size_t pos = FindFrameBoundary<http::Message>(MessageType::kResponse, buf, 1);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(buf.substr(pos), absl::StrCat(kHTTPResp1, kHTTPResp2));
  }

  {
    size_t pos =
        FindFrameBoundary<http::Message>(MessageType::kResponse, buf, kHTTPResp0.length() + 1);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(buf.substr(pos), absl::StrCat(kHTTPResp2));
  }
}

TEST_F(HTTPParserTest, FindNoBoundary) {
  const std::string buf = "This is a bogus string in which there are no HTTP boundaries.";

  {
    size_t pos = FindFrameBoundary<http::Message>(MessageType::kRequest, buf, 0);
    EXPECT_EQ(pos, std::string::npos);
  }

  {
    size_t pos = FindFrameBoundary<http::Message>(MessageType::kResponse, buf, 0);
    EXPECT_EQ(pos, std::string::npos);
  }
}

//=============================================================================
// HTTP Automatic Recovery to Message Boundary Tests
//=============================================================================

TEST_F(HTTPParserTest, ParseReqWithPartialFirstMessageBasicSync) {
  // Test iterates through different offsets into the first message to stress the functionality.
  for (uint32_t offset = 1; offset < kHTTPGetReq0.length(); ++offset) {
    std::string partial_http_get_req0(kHTTPGetReq0.substr(offset, kHTTPGetReq0.length()));
    std::vector<SocketDataEvent> events =
        CreateEvents({partial_http_get_req0, kHTTPPostReq0, kHTTPGetReq1});
    ParserAppendEvents(events);

    std::deque<Message> parsed_messages;
    ParseResult result =
        parser_.ParseMessages(MessageType::kRequest, &parsed_messages, ParseSyncType::Basic);

    EXPECT_EQ(ParseState::kSuccess, result.state);
    EXPECT_THAT(parsed_messages,
                ElementsAre(HTTPPostReq0ExpectedMessage(), HTTPGetReq0ExpectedMessage()));
  }
}

TEST_F(HTTPParserTest, ParseRespWithPartialFirstMessageBasicSync) {
  // Test iterates through different offsets into the first message to stress the functionality.
  for (uint32_t offset = 1; offset < kHTTPResp0.length(); ++offset) {
    std::string partial_http_resp0(kHTTPResp0.substr(offset, kHTTPResp0.length()));
    std::vector<SocketDataEvent> events =
        CreateEvents({partial_http_resp0, kHTTPResp1, kHTTPResp2});
    ParserAppendEvents(events);

    std::deque<Message> parsed_messages;
    ParseResult result =
        parser_.ParseMessages(MessageType::kResponse, &parsed_messages, ParseSyncType::Basic);

    EXPECT_EQ(ParseState::kSuccess, result.state);
    EXPECT_THAT(parsed_messages,
                ElementsAre(HTTPResp1ExpectedMessage(), HTTPResp2ExpectedMessage()));
  }
}

TEST_F(HTTPParserTest, ParseReqWithPartialFirstMessageNoSync) {
  const size_t offset = 4;
  std::string partial_http_get_req0(kHTTPGetReq0.substr(offset, kHTTPGetReq0.length()));
  std::vector<SocketDataEvent> events =
      CreateEvents({partial_http_get_req0, kHTTPPostReq0, kHTTPGetReq1});

  ParserAppendEvents(events);
  std::deque<Message> parsed_messages;
  ParseResult result =
      parser_.ParseMessages(MessageType::kRequest, &parsed_messages, ParseSyncType::None);

  EXPECT_EQ(ParseState::kInvalid, result.state);
  EXPECT_TRUE(parsed_messages.empty());
}

TEST_F(HTTPParserTest, ParseRespWithPartialFirstMessageNoSync) {
  size_t offset = 1;
  std::string partial_http_resp0(kHTTPResp0.substr(offset, kHTTPResp0.length()));
  std::vector<SocketDataEvent> events = CreateEvents({partial_http_resp0, kHTTPResp1, kHTTPResp2});

  ParserAppendEvents(events);
  std::deque<Message> parsed_messages;
  ParseResult result =
      parser_.ParseMessages(MessageType::kResponse, &parsed_messages, ParseSyncType::None);

  EXPECT_EQ(ParseState::kInvalid, result.state);
  EXPECT_TRUE(parsed_messages.empty());
}

TEST_F(HTTPParserTest, ParseReqWithPartialFirstMessageAggressiveSync) {
  const size_t offset = 4;
  std::string partial_http_get_req0(kHTTPGetReq0.substr(offset, kHTTPGetReq0.length()));
  std::vector<SocketDataEvent> events =
      CreateEvents({partial_http_get_req0, kHTTPPostReq0, kHTTPGetReq1});

  ParserAppendEvents(events);
  std::deque<Message> parsed_messages;
  ParseResult result =
      parser_.ParseMessages(MessageType::kRequest, &parsed_messages, ParseSyncType::Aggressive);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_THAT(parsed_messages,
              ElementsAre(HTTPPostReq0ExpectedMessage(), HTTPGetReq0ExpectedMessage()));
}

TEST_F(HTTPParserTest, ParseRespWithPartialFirstMessageAggressiveSync) {
  size_t offset = 1;
  std::string partial_http_resp0(kHTTPResp0.substr(offset, kHTTPResp0.length()));
  std::vector<SocketDataEvent> events = CreateEvents({partial_http_resp0, kHTTPResp1, kHTTPResp2});

  ParserAppendEvents(events);
  std::deque<Message> parsed_messages;
  ParseResult result =
      parser_.ParseMessages(MessageType::kResponse, &parsed_messages, ParseSyncType::Aggressive);

  EXPECT_EQ(ParseState::kSuccess, result.state);
  EXPECT_THAT(parsed_messages, ElementsAre(HTTPResp1ExpectedMessage(), HTTPResp2ExpectedMessage()));
}

}  // namespace http
}  // namespace stirling
}  // namespace pl
