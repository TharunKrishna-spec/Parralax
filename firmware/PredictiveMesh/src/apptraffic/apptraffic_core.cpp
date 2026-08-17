#include "apptraffic_core.h"
#include <string.h>

namespace apptraffic_core {

namespace {

// Wire format for one application DATA payload — see apptraffic_core.h's
// file header for why this packed struct lives here rather than in the
// Arduino adapter (the usual RouteAdWire/AckWire precedent). `packed` for
// the same reason as every other MeshPacket-payload wire struct in this
// project (routing.cpp's RouteAdWire, reliability.cpp's AckWire): a
// MeshPacket's payload[] is a byte array with no alignment guarantee
// beyond 1 byte at any offset, so the compiler must not assume natural
// alignment when loading/storing the uint16_t/uint32_t fields.
#pragma pack(push, 1)
struct DataWire {
  uint16_t appSeq;
  uint16_t potValue;
  uint16_t ldrValue;
  uint32_t timestampMs;
};
#pragma pack(pop)

static_assert(sizeof(DataWire) == DATA_WIRE_SIZE, "DataWire layout must match DATA_WIRE_SIZE exactly");

}  // namespace

uint8_t encodeData(uint16_t appSeq, uint16_t potValue, uint16_t ldrValue, uint32_t timestampMs, uint8_t* out,
                    uint8_t outCap) {
  if (out == nullptr || outCap < DATA_WIRE_SIZE) return 0;

  DataWire wire;
  wire.appSeq = appSeq;
  wire.potValue = potValue;
  wire.ldrValue = ldrValue;
  wire.timestampMs = timestampMs;
  memcpy(out, &wire, DATA_WIRE_SIZE);
  return DATA_WIRE_SIZE;
}

bool decodeData(const uint8_t* payload, uint8_t len, DecodedData* decoded) {
  if (payload == nullptr || decoded == nullptr || len < DATA_WIRE_SIZE) return false;

  DataWire wire;
  memcpy(&wire, payload, DATA_WIRE_SIZE);
  decoded->appSeq = wire.appSeq;
  decoded->potValue = wire.potValue;
  decoded->ldrValue = wire.ldrValue;
  decoded->timestampMs = wire.timestampMs;
  return true;
}

void init(State& state) {
  state.appSeqCounter = 0;
  state.priorityPending = false;
}

void requestPriority(State& state) {
  state.priorityPending = true;
}

SendDecision buildSendDecision(State& state) {
  bool priority = state.priorityPending;
  state.priorityPending = false;  // one-shot: consumed regardless of outcome

  SendDecision d;
  d.destination = NODE_S;
  d.trafficClass = priority ? TrafficClass::PRIORITY : TrafficClass::NORMAL;
  d.priority = priority;
  return d;
}

uint16_t nextAppSeq(State& state) {
  return state.appSeqCounter++;
}

}  // namespace apptraffic_core
