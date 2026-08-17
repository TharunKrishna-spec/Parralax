#include "espnow_transport.h"
#include "../config.h"
#include "../core/logger.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

namespace {

transport::RxCallback g_rxCallback = nullptr;
transport::TxCallback g_txCallback = nullptr;

const uint8_t BROADCAST_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// Core 3.x (ESP-IDF >= 5.1) receive callback. RSSI comes from
// info->rx_ctrl->rssi — this is the one fact the whole predictor layer
// will depend on later, so it is read directly and never invented or
// defaulted. `info` and `info->rx_ctrl` are guaranteed non-null by the
// ESP-NOW callback contract (the driver always populates both before
// invoking this callback), so this trusts that guarantee rather than
// defensively checking for a condition the API contract rules out.
void onEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  int8_t rssi = info->rx_ctrl->rssi;

  char macStr[18];
  logger::macToStr(info->src_addr, macStr);
  logger::rx(macStr, rssi, static_cast<size_t>(len));

  if (g_rxCallback != nullptr) {
    transport::RxEvent evt{};
    memcpy(evt.mac, info->src_addr, 6);
    evt.rssi = rssi;
    evt.data = data;
    evt.len = static_cast<size_t>(len);
    evt.timestamp_ms = millis();
    g_rxCallback(evt);
  }
}

void onEspNowSent(const uint8_t* mac, esp_now_send_status_t status) {
  bool ok = (status == ESP_NOW_SEND_SUCCESS);

  char macStr[18];
  logger::macToStr(mac, macStr);
  logger::tx(macStr, ok);

  if (g_txCallback != nullptr) {
    transport::TxEvent evt{};
    memcpy(evt.mac, mac, 6);
    evt.success = ok;
    evt.timestamp_ms = millis();
    g_txCallback(evt);
  }
}

}  // namespace

namespace transport {

Status begin(RxCallback onRx, TxCallback onTx) {
  g_rxCallback = onRx;
  g_txCallback = onTx;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  logger::info("WiFi station mode set");

  esp_err_t chErr = esp_wifi_set_channel(MESH_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (chErr != ESP_OK) {
    logger::error("Failed to set WiFi channel %d (err=%d)", MESH_WIFI_CHANNEL, static_cast<int>(chErr));
    return Status::ERR_CHANNEL;
  }
  logger::info("WiFi channel fixed to %d", MESH_WIFI_CHANNEL);

  if (esp_now_init() != ESP_OK) {
    logger::error("esp_now_init() failed");
    return Status::ERR_ESPNOW_INIT;
  }
  logger::info("ESP-NOW initialized");

  if (esp_now_register_recv_cb(onEspNowRecv) != ESP_OK ||
      esp_now_register_send_cb(onEspNowSent) != ESP_OK) {
    logger::error("Failed to register ESP-NOW callbacks");
    return Status::ERR_CALLBACK_REGISTER;
  }
  logger::info("ESP-NOW callbacks registered");

  return Status::OK;
}

bool addPeer(const uint8_t mac[6]) {
  if (esp_now_is_peer_exist(mac)) return true;

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = MESH_WIFI_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;

  char macStr[18];
  logger::macToStr(mac, macStr);

  esp_err_t err = esp_now_add_peer(&peer);
  if (err == ESP_OK) {
    logger::info("Peer added: %s", macStr);
    return true;
  }
  logger::error("Failed to add peer %s (err=%d)", macStr, static_cast<int>(err));
  return false;
}

bool addBroadcastPeer() { return addPeer(BROADCAST_MAC); }

bool removePeer(const uint8_t mac[6]) {
  return esp_now_del_peer(mac) == ESP_OK;
}

bool send(const uint8_t mac[6], const uint8_t* data, size_t len) {
  esp_err_t err = esp_now_send(mac, data, len);
  if (err != ESP_OK) {
    char macStr[18];
    logger::macToStr(mac, macStr);
    logger::warn("esp_now_send() to %s rejected immediately (err=%d)", macStr, static_cast<int>(err));
    return false;
  }
  return true;
}

}  // namespace transport
