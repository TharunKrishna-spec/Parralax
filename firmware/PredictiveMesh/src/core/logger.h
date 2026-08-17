#pragma once
#include <stdint.h>
#include <stddef.h>

// ============================================================
// Minimal structured Serial logger.
//
// Deliberately small — four levels, one line format, no ring buffers or
// log sinks. Its only job is to make hardware bring-up debugging legible:
//
//   [INFO] Node B initialized
//   [INFO] ESP-NOW initialized
//   [INFO] Peer added: A
//   [RX] src=A rssi=-58 len=24
//   [TX] dst=B status=SUCCESS
//
// Placement note: this lives under core/ rather than a new top-level
// folder because it's cross-cutting infrastructure like node_id.h/packet.h,
// not a layer in the §01 architecture stack. See docs/decisions.md.
// ============================================================

enum class LogLevel : uint8_t { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

namespace logger {

// Starts Serial at `baudRate` and gives the USB-CDC/UART a moment to
// enumerate before the first log line is written.
void begin(unsigned long baudRate);

// Suppresses log() calls below this level. Default is INFO (DEBUG hidden).
void setLevel(LogLevel minLevel);

void log(LogLevel level, const char* fmt, ...);
void debug(const char* fmt, ...);
void info(const char* fmt, ...);
void warn(const char* fmt, ...);
void error(const char* fmt, ...);

// Formats mac as "AA:BB:CC:DD:EE:FF" into buf. buf must be >= 18 bytes.
void macToStr(const uint8_t mac[6], char* buf);

// Structured helpers matching the project's documented [RX]/[TX] log shapes.
void rx(const char* srcLabel, int8_t rssi, size_t len);
void tx(const char* dstLabel, bool success);

}  // namespace logger
