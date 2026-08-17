#!/usr/bin/env python3
"""Multi-node WebSocket telemetry bridge for the Mesh Command Console GUI.

Solves a real architecture gap found during the 2026-08-18 GUI audit: the
GUI's own transport is single-source (one `navigator.serial.requestPort()`
or one WebSocket connection at a time — see docs/full-system-audit.md
Phase 12), but every physical board (A, B, C, D, S) runs its own firmware
and emits its own, independently real, self-identified telemetry stream
(every message's `nodeId` is always that board's own `THIS_NODE_ID` — see
telemetry.cpp). The GUI's own state model is ALREADY keyed by real
`nodeId` (`state.firmware.nodes[nodeId]`, `state.firmware.links[from>to]`,
etc.) and already renders however many distinct nodes it receives — the
only missing piece was a transport that can feed it more than one board's
Serial output on one WebSocket connection.

This script opens N real serial ports (one per physical board) and
relays every line from every one of them, VERBATIM AND UNMODIFIED, to
every connected WebSocket client. It never rewrites, retags, or
re-derives a `nodeId` — each board's firmware already stamps its own real
identity on every line it emits, so simple multiplexing is enough for the
GUI's real, already-per-node state model to build up a genuine live
picture of all five nodes.

This file lives OUTSIDE gui-main/ deliberately — gui-main/ (the HTML
console, the two Python bridge/mock scripts, and the frozen telemetry
contract) is a teammate's code and is never edited by this project's own
tooling (see CLAUDE.md's "Workflow rules"). This script only ever
CONNECTS to the GUI's own, unmodified "Connect via Bridge" feature over a
plain WebSocket — the same protocol gui-main/gui-main/serial-bridge.py
already uses for its own single-port case — it does not import, read, or
depend on any file under gui-main/.

Usage:
  pip install websockets pyserial
  python3 tools/multi-node-bridge.py --port A=COM3 --port B=COM4 \
      --port C=COM5 --port D=COM6 --port S=COM7 --baud 115200
  # then in the GUI: Connect via Bridge -> ws://localhost:8765

  For a hardware-free rehearsal of the multi-node aggregation itself
  (NOT a substitute for real firmware — see docs/known-issues.md):
    python3 tools/multi-node-bridge.py --mock A,B,C,D,S

Each `--port LABEL=PATH` pairing is for this tool's own startup log
messages only (so an operator can tell which physical port maps to which
board at a glance) — the label is never written into any relayed line;
the real `nodeId` in each board's own JSON is always the one that
reaches the GUI.
"""
import argparse
import asyncio
import json
import random
import sys
import time

try:
    import websockets
except ImportError as exc:
    raise SystemExit("Missing dependency: pip install websockets") from exc


CLIENTS = set()


async def client_handler(websocket, *_):
    CLIENTS.add(websocket)
    print(f"client connected ({len(CLIENTS)} active)", file=sys.stderr)
    try:
        await websocket.wait_closed()
    finally:
        CLIENTS.discard(websocket)
        print(f"client disconnected ({len(CLIENTS)} active)", file=sys.stderr)


async def broadcast(line):
    """Send independently to every client; one broken client cannot stop
    any other node's stream."""
    if not CLIENTS:
        return
    sockets = list(CLIENTS)
    results = await asyncio.gather(*(socket.send(line) for socket in sockets), return_exceptions=True)
    for socket, result in zip(sockets, results):
        if isinstance(result, Exception):
            CLIENTS.discard(socket)
            try:
                await socket.close()
            except Exception:
                pass


async def serial_node_source(label, port_path, baud):
    """Relays one physical board's real Serial output, line for line,
    completely unmodified. Never blocks the other ports — each runs as
    its own independent asyncio task, matching serial-bridge.py's own
    single-port precedent."""
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("Missing dependency: pip install pyserial") from exc
    serial_port = serial.Serial(port_path, baudrate=baud, timeout=.2)
    print(f"[{label}] serial source open: {port_path} @ {baud}", file=sys.stderr)
    try:
        while True:
            raw = await asyncio.to_thread(serial_port.readline)
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            if not line:
                continue
            print(line, flush=True)
            await broadcast(line)
    finally:
        serial_port.close()
        print(f"[{label}] serial source closed: {port_path}", file=sys.stderr)


def _mock_line(node_id, boot_id, seq):
    """A minimal, honestly-labeled, self-contained mock line for
    rehearsing the AGGREGATION path itself (multiple simultaneous nodeIds
    on one WebSocket) — not a substitute for real firmware telemetry, and
    not imported from gui-main/'s own serial-mock.py (kept fully
    independent of that off-limits directory)."""
    return json.dumps({
        "protocolVersion": "mesh-json/v1", "type": "HEARTBEAT", "nodeId": node_id,
        "bootId": boot_id, "seq": seq, "timestampMs": int(time.time() * 1000) % 1000000,
        "payload": {"uptimeMs": seq * 1000},
    }, separators=(",", ":"))


async def mock_node_source(label, node_id, rng):
    boot_id = f"{node_id.lower()}-mock-{rng.randint(1000, 9999)}"
    seq = 0
    print(f"[{label}] mock source started for nodeId={node_id}", file=sys.stderr)
    while True:
        line = _mock_line(node_id, boot_id, seq)
        seq += 1
        print(line, flush=True)
        await broadcast(line)
        await asyncio.sleep(1.0)


async def run(args):
    tasks = []
    async with websockets.serve(client_handler, "localhost", args.listen_port):
        print(f"Multi-node WebSocket bridge listening at ws://localhost:{args.listen_port}", flush=True)
        print("Dashboard: open mesh-command-console.html, then Connect via Bridge -> "
              f"ws://localhost:{args.listen_port}", flush=True)

        if args.mock:
            rng = random.Random(args.seed)
            for node_id in args.mock.split(","):
                node_id = node_id.strip()
                if node_id:
                    tasks.append(asyncio.create_task(mock_node_source(node_id, node_id, rng)))
        else:
            for label, port_path in args.port:
                tasks.append(asyncio.create_task(serial_node_source(label, port_path, args.baud)))

        if not tasks:
            raise SystemExit("No --port given and --mock not set — nothing to bridge. See --help.")

        try:
            await asyncio.gather(*tasks)
        finally:
            for t in tasks:
                if not t.done():
                    t.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)


def _parse_port_arg(value):
    if "=" not in value:
        raise argparse.ArgumentTypeError("expected LABEL=PORTPATH, e.g. A=COM3 or A=/dev/ttyUSB0")
    label, path = value.split("=", 1)
    if not label or not path:
        raise argparse.ArgumentTypeError("both LABEL and PORTPATH must be non-empty")
    return (label, path)


def main():
    parser = argparse.ArgumentParser(
        description="Multiplex several real per-node serial ports onto one WebSocket for the Mesh Command Console GUI's existing, unmodified 'Connect via Bridge' feature.")
    parser.add_argument("--listen-port", type=int, default=8765, help="WebSocket port the GUI connects to (default: 8765)")
    parser.add_argument("--port", action="append", type=_parse_port_arg, default=[],
                         help="LABEL=PORTPATH, repeatable — one per physical board, e.g. --port A=COM3 --port S=COM7")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--mock", default=None,
                         help="comma-separated nodeIds to rehearse the aggregation path itself without hardware, e.g. --mock A,B,C,D,S (NOT a substitute for real firmware telemetry)")
    parser.add_argument("--seed", type=int, default=None)
    args = parser.parse_args()

    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        print("\nBridge stopped cleanly.", file=sys.stderr)


if __name__ == "__main__":
    main()
