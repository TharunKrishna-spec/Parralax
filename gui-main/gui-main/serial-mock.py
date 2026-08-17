#!/usr/bin/env python3
"""Best-effort newline JSON source for Mesh Command Console WebSerial testing.

Linux/macOS use a PTY and symlink. Windows needs a virtual-pair tool such as
com0com instead. Browser access to software-only PTYs is platform-dependent;
for a reliable fallback, pipe/copy the printed JSON into a USB-serial loopback.
"""
import argparse
import json
import os
import pty
import random
import signal
import sys
import time
from pathlib import Path


def build_cycle(rng, speed):
    """Yield (phase duration seconds, telemetry object) at approximately 150 ms."""
    tick = 0.15 / speed
    phases = []
    phases += [(8.0, {"linkAB": .90, "route": "ABS", "flagC": "clean", "pdr": .99})]
    # 2 seconds of degradation crosses the dashboard's .58 predictive threshold.
    for i in range(14):
        phases.append((.15, {"linkAB": .90 - i * .045, "route": "ABS", "flagC": "clean", "pdr": .985}))
    phases += [(.15, {"linkAB": .30, "route": "ACDS", "flagC": "clean", "pdr": .96,
                      "rerouteLeadMs": rng.randint(150, 400)})]
    phases += [(5.0, {"linkAB": .30, "route": "ACDS", "flagC": "clean", "pdr": .96})]
    phases += [(1.5, {"linkAB": .30, "route": "ACDS", "flagC": "spike", "pdr": .95})]
    phases += [(3.0, {"linkAB": .30, "route": "ACDS", "flagC": "stuck", "pdr": .95})]
    phases += [(.15, {"linkAB": .30, "route": "AS", "trafficType": "priority", "flagC": "stuck", "pdr": .95})]
    phases += [(2.0, {"linkAB": .90, "route": "ABS", "flagC": "clean", "pdr": .99})]
    for duration, payload in phases:
        yield duration, tick, payload


def main():
    parser = argparse.ArgumentParser(description="PTY JSON serial mock for Mesh Command Console")
    parser.add_argument("--speed", type=float, default=1.0, help="cycle speed multiplier (default: 1.0)")
    parser.add_argument("--seed", type=int, default=None, help="random seed for reproducible lead time")
    args = parser.parse_args()
    if args.speed <= 0:
        parser.error("--speed must be greater than zero")
    rng = random.Random(args.seed)
    master, slave = pty.openpty()
    slave_path = os.ttyname(slave)
    alias = Path("/tmp/mesh-command-console-serial")
    try:
        if alias.exists() or alias.is_symlink():
            alias.unlink()
        alias.symlink_to(slave_path)
    except OSError as exc:
        print(f"warning: could not create symlink {alias}: {exc}", file=sys.stderr)
    print(f"Mock serial slave: {slave_path}")
    print(f"Select that path in Chrome/Edge's Connect Hardware picker (best-effort: WebSerial may reject PTYs).")
    print(f"Stable alias: {alias}")
    print("Fallback: use a USB-serial loopback/adapter if your browser cannot expose a virtual PTY.")
    print("Stop with Ctrl+C; the PTY and temporary symlink will be closed/removed cleanly.")
    sys.stdout.flush()
    running = True
    def stop(_sig, _frame):
        nonlocal running
        running = False
    signal.signal(signal.SIGINT, stop)
    try:
        while running:
            for duration, tick, template in build_cycle(rng, args.speed):
                end = time.monotonic() + duration / args.speed
                while running and time.monotonic() < end:
                    payload = dict(template)
                    line = json.dumps(payload, separators=(",", ":")) + "\n"
                    os.write(master, line.encode("utf-8"))
                    print(line, end="")
                    sys.stdout.flush()
                    time.sleep(tick)
    except (BrokenPipeError, OSError) as exc:
        print(f"serial output stopped: {exc}", file=sys.stderr)
    finally:
        os.close(master)
        os.close(slave)
        try:
            if alias.is_symlink():
                alias.unlink()
        except OSError:
            pass


if __name__ == "__main__":
    main()
