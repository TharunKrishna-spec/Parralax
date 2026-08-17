#!/usr/bin/env python3
"""Local WebSocket telemetry bridge for Mesh Command Console.

Test first with the synchronized mock source:
  pip install websockets
  python3 serial-bridge.py --source mock --speed 4 --seed 7
Then open mesh-command-console.html and set Connect via Bridge to
ws://localhost:8765. For real firmware, use --source serial:/dev/ttyUSB0
--baud 115200 (pyserial is required for that mode).
"""
import argparse
import asyncio
import importlib.util
import json
import random
import sys
from pathlib import Path

try:
    import websockets
except ImportError as exc:
    raise SystemExit("Missing dependency: pip install websockets") from exc


CLIENTS = set()


def load_mock_cycle():
    """Use serial-mock.py directly so both mock paths share one scripted timeline."""
    path = Path(__file__).with_name("serial-mock.py")
    spec = importlib.util.spec_from_file_location("mesh_serial_mock", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.build_cycle


async def client_handler(websocket, *_):
    CLIENTS.add(websocket)
    print(f"client connected ({len(CLIENTS)} active)", file=sys.stderr)
    try:
        await websocket.wait_closed()
    finally:
        CLIENTS.discard(websocket)
        print(f"client disconnected ({len(CLIENTS)} active)", file=sys.stderr)


async def broadcast(line):
    """Send independently to every client; one broken client cannot stop the source."""
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


async def mock_source(speed, seed):
    build_cycle = load_mock_cycle()
    rng = random.Random(seed)
    while True:
        for duration, tick, template in build_cycle(rng, speed):
            end = asyncio.get_running_loop().time() + duration / speed
            while asyncio.get_running_loop().time() < end:
                line = json.dumps(dict(template), separators=(",", ":"))
                print(line, flush=True)
                await broadcast(line)
                await asyncio.sleep(tick)


async def serial_source(port_path, baud):
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("Missing dependency for serial source: pip install pyserial") from exc
    serial_port = serial.Serial(port_path, baudrate=baud, timeout=.2)
    print(f"serial source open: {port_path} @ {baud}", file=sys.stderr)
    try:
        while True:
            raw = await asyncio.to_thread(serial_port.readline)
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            if line:
                print(line, flush=True)
                await broadcast(line)
    finally:
        serial_port.close()


async def run(args):
    source_task = None
    async with websockets.serve(client_handler, "localhost", args.port):
        print(f"WebSocket bridge listening at ws://localhost:{args.port}", flush=True)
        print("Dashboard: open mesh-command-console.html, then Connect via Bridge → ws://localhost:"
              f"{args.port}", flush=True)
        if args.source == "mock":
            source_task = asyncio.create_task(mock_source(args.speed, args.seed))
        else:
            source_task = asyncio.create_task(serial_source(args.source.removeprefix("serial:"), args.baud))
        try:
            await source_task
        finally:
            if source_task and not source_task.done():
                source_task.cancel()
                await asyncio.gather(source_task, return_exceptions=True)


def main():
    parser = argparse.ArgumentParser(description="WebSocket bridge for mesh telemetry")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--source", default="mock", help="mock or serial:/path/to/device")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--speed", type=float, default=1.0)
    parser.add_argument("--seed", type=int, default=None)
    args = parser.parse_args()
    if args.speed <= 0:
        parser.error("--speed must be greater than zero")
    if args.source != "mock" and not args.source.startswith("serial:"):
        parser.error("--source must be mock or serial:<port>")
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        print("\nBridge stopped cleanly.", file=sys.stderr)


if __name__ == "__main__":
    main()
