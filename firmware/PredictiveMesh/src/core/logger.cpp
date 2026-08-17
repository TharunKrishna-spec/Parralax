#include "logger.h"
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

namespace {

LogLevel g_minLevel = LogLevel::INFO;

const char* levelTag(LogLevel level) {
  switch (level) {
    case LogLevel::DEBUG: return "DEBUG";
    case LogLevel::INFO:  return "INFO";
    case LogLevel::WARN:  return "WARN";
    case LogLevel::ERROR: return "ERROR";
    default:              return "?";
  }
}

void logv(LogLevel level, const char* fmt, va_list args) {
  if (static_cast<uint8_t>(level) < static_cast<uint8_t>(g_minLevel)) return;
  char buf[160];
  vsnprintf(buf, sizeof(buf), fmt, args);
  Serial.print('[');
  Serial.print(levelTag(level));
  Serial.print("] ");
  Serial.println(buf);
}

}  // namespace

namespace logger {

void begin(unsigned long baudRate) {
  Serial.begin(baudRate);
  delay(100);
}

void setLevel(LogLevel minLevel) { g_minLevel = minLevel; }

void log(LogLevel level, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logv(level, fmt, args);
  va_end(args);
}

void debug(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logv(LogLevel::DEBUG, fmt, args);
  va_end(args);
}

void info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logv(LogLevel::INFO, fmt, args);
  va_end(args);
}

void warn(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logv(LogLevel::WARN, fmt, args);
  va_end(args);
}

void error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logv(LogLevel::ERROR, fmt, args);
  va_end(args);
}

void macToStr(const uint8_t mac[6], char* buf) {
  snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void rx(const char* srcLabel, int8_t rssi, size_t len) {
  if (static_cast<uint8_t>(LogLevel::INFO) < static_cast<uint8_t>(g_minLevel)) return;
  Serial.printf("[RX] src=%s rssi=%d len=%u\n", srcLabel, rssi, static_cast<unsigned>(len));
}

void tx(const char* dstLabel, bool success) {
  if (static_cast<uint8_t>(LogLevel::INFO) < static_cast<uint8_t>(g_minLevel)) return;
  Serial.printf("[TX] dst=%s status=%s\n", dstLabel, success ? "SUCCESS" : "FAIL");
}

}  // namespace logger
