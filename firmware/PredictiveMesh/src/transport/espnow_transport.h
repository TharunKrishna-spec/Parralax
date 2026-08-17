#pragma once
#include <stdint.h>
#include <stddef.h>

// ============================================================
// ESP-NOW transport layer.
//
// Owns everything that touches the ESP-NOW / WiFi radio APIs directly:
// init, channel fix, peer table, send, and the recv/send callbacks. Every
// other module (routing, reliability, predictor, ...) is meant to consume
// RxEvent/TxEvent through the callbacks registered in begin() instead of
// calling esp_now_* itself — that's the whole point of this module
// existing, per implementation-guide.html's "TRANSPORT LAYER" contract.
//
// Uses Arduino-ESP32 core 3.x (ESP-IDF >= 5.1) callback signatures:
//   recv: void(*)(const esp_now_recv_info_t*, const uint8_t*, int)
//   RSSI: info->rx_ctrl->rssi
// This is a hard toolchain requirement, not a style choice — core 2.x's
// recv callback only exposes the sender MAC, no RSSI, and the whole
// predictor layer depends on RSSI existing here.
// ============================================================

namespace transport {

struct RxEvent {
  uint8_t mac[6];
  int8_t rssi;
  const uint8_t* data;  // points at ESP-NOW's own receive buffer; valid only for the duration of the callback
  size_t len;
  uint32_t timestamp_ms;  // millis() at the moment the callback fired
};

struct TxEvent {
  uint8_t mac[6];
  bool success;
  uint32_t timestamp_ms;
};

typedef void (*RxCallback)(const RxEvent& event);
typedef void (*TxCallback)(const TxEvent& event);

enum class Status {
  OK,
  ERR_CHANNEL,
  ERR_ESPNOW_INIT,
  ERR_CALLBACK_REGISTER,
};

// Brings up WiFi in station mode, fixes the channel to MESH_WIFI_CHANNEL,
// initializes ESP-NOW, and registers the given application-level
// callbacks. Call exactly once from setup(), before any addPeer()/send().
Status begin(RxCallback onRx, TxCallback onTx);

// Registers a unicast ESP-NOW peer on the fixed mesh channel. Safe to call
// again for an already-registered MAC (no-op). Returns false on failure.
bool addPeer(const uint8_t mac[6]);

// Registers the ESP-NOW broadcast peer (FF:FF:FF:FF:FF:FF). Requires no
// prior MAC knowledge — this is the Phase 0 bring-up bootstrap used before
// real per-node MACs are known. See docs/decisions.md.
bool addBroadcastPeer();

bool removePeer(const uint8_t mac[6]);

// Sends `len` bytes to `mac`. `mac` must already be a registered peer
// (unicast or broadcast) or esp_now_send() will reject it immediately.
// Final delivery success/failure arrives asynchronously via the TxCallback
// passed to begin() — this return value only reflects whether the send
// call itself was accepted by the radio driver.
bool send(const uint8_t mac[6], const uint8_t* data, size_t len);

}  // namespace transport
