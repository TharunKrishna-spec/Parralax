// Minimal host-side unit test harness for apptraffic_core's pure
// send-decision/payload-encoding logic (Phase 7). Like every other *_core
// test suite in this project, this is NOT a network/radio simulator: it
// never simulates ESP-NOW, a real reliability::send() call, or a real
// sensor read. It only feeds apptraffic_core's pure functions
// hand-constructed values and checks outputs against this phase's own
// documented semantics (see docs/decisions.md). apptraffic_core.h/.cpp
// have zero Arduino/ESP-NOW dependency specifically so this can compile
// and run with a plain host compiler. See docs/testing.md.
//
// Build & run (host g++ - NOT the ESP32 toolchain; run from this file's
// directory):
//   g++ -std=c++17 -Wall -Wextra -I ../src ../src/apptraffic/apptraffic_core.cpp test_apptraffic_core.cpp -o test_apptraffic_core
//   ./test_apptraffic_core

#include "../src/apptraffic/apptraffic_core.h"
#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* description) {
  g_checks++;
  if (!condition) {
    g_failures++;
    std::printf("FAIL: %s\n", description);
  } else {
    std::printf("ok:   %s\n", description);
  }
}

using namespace apptraffic_core;

// ---- 1. Normal application packet construction / destination ----
void test_normal_send_decision() {
  State s;
  init(s);

  SendDecision d = buildSendDecision(s);
  check(d.destination == NODE_S, "a NORMAL send decision addresses NODE_S");
  check(d.trafficClass == TrafficClass::NORMAL, "a fresh state produces NORMAL by default (no trigger fired)");
  check(d.priority == false, "NORMAL decision's priority bool is false, matching reliability::send()'s own param");
}

// ---- 2. Destination is NODE_S regardless of how many decisions are made ----
void test_destination_always_node_s() {
  State s;
  init(s);

  for (int i = 0; i < 20; i++) {
    SendDecision d = buildSendDecision(s);
    if (d.destination != NODE_S) {
      check(false, "every send decision addresses NODE_S, with no exception across repeated calls");
      return;
    }
  }
  check(true, "every send decision addresses NODE_S, with no exception across repeated calls");
}

// ---- 3. Priority trigger produces exactly one PRIORITY decision ----
void test_priority_trigger_is_one_shot() {
  State s;
  init(s);

  requestPriority(s);
  SendDecision first = buildSendDecision(s);
  check(first.trafficClass == TrafficClass::PRIORITY, "a requested priority trigger produces a PRIORITY decision");
  check(first.priority == true, "PRIORITY decision's priority bool is true");
  check(first.destination == NODE_S, "a PRIORITY decision still addresses NODE_S — same fixed demo flow");

  SendDecision second = buildSendDecision(s);
  check(second.trafficClass == TrafficClass::NORMAL,
        "the very next decision after a consumed priority trigger reverts to NORMAL (one-shot, not sticky)");
}

// ---- 4. Multiple requestPriority() calls before consumption still yield exactly one PRIORITY packet ----
void test_priority_trigger_does_not_flood() {
  State s;
  init(s);

  requestPriority(s);
  requestPriority(s);
  requestPriority(s);

  SendDecision first = buildSendDecision(s);
  check(first.trafficClass == TrafficClass::PRIORITY, "three requestPriority() calls before consumption still yield PRIORITY once");

  SendDecision second = buildSendDecision(s);
  check(second.trafficClass == TrafficClass::NORMAL,
        "...and only once — the next decision is NORMAL, not another PRIORITY");
}

// ---- 5. Application sequence counter behavior ----
void test_app_seq_counter_increments() {
  State s;
  init(s);

  uint16_t first = nextAppSeq(s);
  uint16_t second = nextAppSeq(s);
  uint16_t third = nextAppSeq(s);
  check(first == 0, "the app sequence counter starts at 0 on a fresh state");
  check(second == 1, "the app sequence counter increments by exactly 1 per call");
  check(third == 2, "...and again");
}

// ---- 6. Application sequence counter wraps at 65536, same as MeshPacket.sequence ----
void test_app_seq_counter_wraps() {
  State s;
  init(s);
  s.appSeqCounter = 65535;

  uint16_t last = nextAppSeq(s);
  uint16_t wrapped = nextAppSeq(s);
  check(last == 65535, "the counter reaches its maximum uint16_t value");
  check(wrapped == 0, "the counter wraps to 0 after 65535, not to an error/sentinel value");
}

// ---- 7. Application sequence counter is independent of the priority latch ----
void test_app_seq_independent_of_priority() {
  State s;
  init(s);

  requestPriority(s);
  uint16_t seqBeforeDecision = s.appSeqCounter;
  buildSendDecision(s);  // consumes the priority latch, must NOT touch appSeqCounter
  check(s.appSeqCounter == seqBeforeDecision,
        "buildSendDecision() consuming the priority latch does not itself advance the app sequence counter");
}

// ---- 8. Payload encode: correct size, no partial write ----
void test_encode_returns_wire_size() {
  uint8_t buf[DATA_WIRE_SIZE];
  uint8_t written = encodeData(1, 2048, 1500, 123456, buf, sizeof(buf));
  check(written == DATA_WIRE_SIZE, "encodeData writes exactly DATA_WIRE_SIZE bytes on success");
}

// ---- 9. Payload size bounds: encode refuses a too-small buffer, writes nothing ----
void test_encode_refuses_undersized_buffer() {
  uint8_t buf[DATA_WIRE_SIZE - 1];
  memset(buf, 0xAA, sizeof(buf));
  uint8_t written = encodeData(1, 2048, 1500, 123456, buf, sizeof(buf));
  check(written == 0, "encodeData refuses to write into a buffer smaller than DATA_WIRE_SIZE (never partial)");
}

// ---- 10. Payload fits comfortably within the MeshPacket payload capacity ----
void test_payload_fits_packet_capacity() {
  // PACKET_MAX_PAYLOAD (core/packet.h) is 64 bytes; not included directly
  // here (apptraffic_core stays free of core/packet.h, matching every
  // other *_core module's Arduino/wire-format independence), so this
  // checks against the literal documented in docs/protocol.md instead.
  const uint8_t PACKET_MAX_PAYLOAD_DOCUMENTED = 64;
  check(DATA_WIRE_SIZE <= PACKET_MAX_PAYLOAD_DOCUMENTED,
        "DATA_WIRE_SIZE (10 bytes) fits comfortably within MeshPacket's 64-byte payload capacity");
}

// ---- 11. Sensor values encoded/decoded correctly (round-trip) ----
void test_encode_decode_round_trip() {
  uint8_t buf[DATA_WIRE_SIZE];
  uint8_t written = encodeData(4242, 4095, 0, 0xDEADBEEF, buf, sizeof(buf));
  check(written == DATA_WIRE_SIZE, "round-trip setup: encode succeeds");

  DecodedData decoded{};
  bool ok = decodeData(buf, DATA_WIRE_SIZE, &decoded);
  check(ok, "decodeData succeeds on a correctly-sized buffer");
  check(decoded.appSeq == 4242, "decoded appSeq matches the encoded value exactly");
  check(decoded.potValue == 4095, "decoded potValue matches the encoded value exactly (max 12-bit ADC reading)");
  check(decoded.ldrValue == 0, "decoded ldrValue matches the encoded value exactly (min 12-bit ADC reading)");
  check(decoded.timestampMs == 0xDEADBEEF, "decoded timestampMs matches the encoded value exactly (full uint32_t range)");
}

// ---- 12. No malformed packet construction: decode refuses a short/corrupt payload ----
void test_decode_refuses_short_payload() {
  uint8_t buf[DATA_WIRE_SIZE];
  encodeData(1, 2, 3, 4, buf, sizeof(buf));

  DecodedData decoded{};
  decoded.appSeq = 0xBEEF;  // sentinel — must remain untouched on refusal
  bool ok = decodeData(buf, DATA_WIRE_SIZE - 1, &decoded);
  check(!ok, "decodeData refuses a payload shorter than DATA_WIRE_SIZE");
  check(decoded.appSeq == 0xBEEF, "a refused decode leaves the output struct untouched, never partially filled");
}

// ---- 13. No malformed packet construction: decode refuses a null payload ----
void test_decode_refuses_null_payload() {
  DecodedData decoded{};
  bool ok = decodeData(nullptr, DATA_WIRE_SIZE, &decoded);
  check(!ok, "decodeData refuses a null payload pointer rather than dereferencing it");
}

// ---- 14. No malformed packet construction: encode refuses a null output buffer ----
void test_encode_refuses_null_buffer() {
  uint8_t written = encodeData(1, 2, 3, 4, nullptr, DATA_WIRE_SIZE);
  check(written == 0, "encodeData refuses a null output buffer rather than dereferencing it");
}

}  // namespace

int main() {
  test_normal_send_decision();
  test_destination_always_node_s();
  test_priority_trigger_is_one_shot();
  test_priority_trigger_does_not_flood();
  test_app_seq_counter_increments();
  test_app_seq_counter_wraps();
  test_app_seq_independent_of_priority();
  test_encode_returns_wire_size();
  test_encode_refuses_undersized_buffer();
  test_payload_fits_packet_capacity();
  test_encode_decode_round_trip();
  test_decode_refuses_short_payload();
  test_decode_refuses_null_payload();
  test_encode_refuses_null_buffer();

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
