#!/usr/bin/env python3
import argparse
import pathlib
import sys
import time

import serial

from cdc_transfer_test import send_command, upload_file
from make_sdmj import (
    DEFAULT_DURATION_S,
    DEFAULT_FPS,
    DEFAULT_HEIGHT,
    DEFAULT_QUALITY,
    DEFAULT_WIDTH,
    build_sdmj,
)


ACTIVE_SCREEN_SAVER_PATH = "/screensavers/active.sdmj"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert media to .sdmj, upload it, and request SS STATUS"
    )
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--output", default="build/screensaver/active.sdmj", type=pathlib.Path)
    parser.add_argument("--port", help="Serial port to upload to")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--wait", type=float, default=1.0)
    parser.add_argument("--chunk-size", type=int, default=2048)
    parser.add_argument("--inter-chunk-ms", type=float, default=0.0)
    parser.add_argument("--retry", type=int, default=2)
    parser.add_argument("--fps", type=int, default=DEFAULT_FPS)
    parser.add_argument("--duration", type=float, default=DEFAULT_DURATION_S)
    parser.add_argument("--quality", type=int, default=DEFAULT_QUALITY)
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    parser.add_argument("--convert-only", action="store_true")
    args = parser.parse_args()

    stats = build_sdmj(
        args.input,
        args.output,
        width=args.width,
        height=args.height,
        fps=args.fps,
        duration_s=args.duration,
        quality=args.quality,
    )
    print(
        f"Built {args.output} "
        f"({stats['frames']} frames, {stats['bytes']} bytes, max frame {stats['max_frame']} bytes)"
    )

    if args.convert_only:
        return 0
    if not args.port:
        raise SystemExit("--port is required unless --convert-only is set")

    with serial.Serial(args.port, args.baud, timeout=0.25) as port:
        time.sleep(args.wait)
        port.reset_input_buffer()
        response = send_command(port, "PING", args.timeout)
        if response != "CDC:PONG":
            raise RuntimeError(f"Expected CDC:PONG, got: {response}")

        upload_file(
            port,
            args.output,
            ACTIVE_SCREEN_SAVER_PATH,
            args.timeout,
            args.chunk_size,
            args.retry,
            args.inter_chunk_ms,
        )
        response = send_command(port, "SS STATUS", args.timeout)
        if not response.startswith("CDC:SS STATUS ok=1"):
            raise RuntimeError(f"Screensaver status failed: {response}")

    print("Done")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pylint: disable=broad-except
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(1)
