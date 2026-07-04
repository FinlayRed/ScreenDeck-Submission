#!/usr/bin/env python3
import argparse
import pathlib
import struct
import sys
import time

import serial


def read_cdc_line(port: serial.Serial, timeout_s: float, echo: bool = True) -> str:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        raw = port.readline()
        if not raw:
            continue

        line = raw.decode("utf-8", errors="replace").strip()
        if not line:
            continue

        if echo:
            print(line, flush=True)
        if line.startswith("CDC:"):
            return line

    raise TimeoutError("Timed out waiting for CDC response")


def send_command(port: serial.Serial, command: str, timeout_s: float) -> str:
    print(f">>> {command}", flush=True)
    port.write((command + "\n").encode("utf-8"))
    port.flush()
    return read_cdc_line(port, timeout_s)


def upload_file(
    port: serial.Serial,
    local_path: pathlib.Path,
    remote_path: str,
    timeout_s: float,
    chunk_size: int,
    retries: int,
    inter_chunk_ms: float,
    chunk_ack: bool,
) -> None:
    data = local_path.read_bytes()

    if not data:
        raise RuntimeError(f"{local_path} is empty")

    if chunk_ack:
        upload_file_chunked(port, data, remote_path, timeout_s, chunk_size)
        return

    for attempt in range(retries + 1):
        response = send_command(port, f"PUT {remote_path} {len(data)}", timeout_s)
        if not response.startswith("CDC:READY PUT"):
            raise RuntimeError(f"Expected READY, got: {response}")

        print(f">>> sending {len(data)} bytes in chunks of {chunk_size}", flush=True)
        offset = 0
        next_progress = 1024 * 1024
        while offset < len(data):
            end = min(offset + chunk_size, len(data))
            chunk = data[offset:end]
            written = port.write(chunk)
            if written != len(chunk):
                raise RuntimeError(
                    f"Short serial write at {offset}: wrote {written}, expected {len(chunk)}"
                )

            offset = end
            if offset >= next_progress or offset == len(data):
                print(f">>> sent {offset}/{len(data)} bytes", flush=True)
                next_progress += 1024 * 1024
            if inter_chunk_ms > 0:
                time.sleep(inter_chunk_ms / 1000.0)

        port.flush()

        response = read_cdc_line(port, timeout_s)
        if response.startswith("CDC:OK PUT"):
            return

        if response.startswith("CDC:ERR PUT TIMEOUT") and attempt < retries:
            print(
                f"Retrying upload ({attempt + 1}/{retries}) after PUT TIMEOUT",
                flush=True,
            )
            time.sleep(0.2)
            port.reset_input_buffer()
            continue

        raise RuntimeError(f"Expected OK PUT, got: {response}")

    raise RuntimeError("Upload retries exhausted")


def upload_file_chunked(
    port: serial.Serial,
    data: bytes,
    remote_path: str,
    timeout_s: float,
    chunk_size: int,
) -> None:
    if chunk_size > 1024:
        raise RuntimeError("--chunk-size must be <= 1024 when --chunk-ack is used")

    response = send_command(port, f"PUTC {remote_path} {len(data)}", timeout_s)
    if not response.startswith("CDC:READY PUTC"):
        raise RuntimeError(f"Expected READY PUTC, got: {response}")

    print(f">>> sending {len(data)} bytes in acked chunks of {chunk_size}", flush=True)
    offset = 0
    next_progress = 1024 * 1024
    while offset < len(data):
        end = min(offset + chunk_size, len(data))
        chunk = data[offset:end]
        port.write(struct.pack("<H", len(chunk)))
        written = port.write(chunk)
        if written != len(chunk):
            raise RuntimeError(
                f"Short serial write at {offset}: wrote {written}, expected {len(chunk)}"
            )
        port.flush()

        response = read_cdc_line(port, timeout_s, echo=False)
        if end == len(data):
            print(response, flush=True)
            if not response.startswith("CDC:OK PUTC"):
                raise RuntimeError(f"Expected OK PUTC, got: {response}")
        else:
            parts = response.split()
            if len(parts) != 3 or parts[0] != "CDC:ACK" or parts[1] != "PUTC":
                raise RuntimeError(f"Expected ACK PUTC, got: {response}")
            try:
                received = int(parts[2])
            except ValueError as exc:
                raise RuntimeError(f"Invalid ACK offset: {parts[2]}") from exc
            if received != end:
                raise RuntimeError(f"ACK offset mismatch: {received} != {end}")

        offset = end
        if offset >= next_progress or offset == len(data):
            print(f">>> sent {offset}/{len(data)} bytes", flush=True)
            next_progress += 1024 * 1024


def download_file(
    port: serial.Serial,
    remote_path: str,
    local_path: pathlib.Path,
    timeout_s: float,
) -> None:
    response = send_command(port, f"GET {remote_path}", timeout_s)
    if response.startswith("CDC:ERR GET NOT_FOUND"):
        raise RuntimeError(f"Remote file not found: {remote_path}")

    parts = response.split()
    if len(parts) != 4 or parts[0] != "CDC:READY" or parts[1] != "GET":
        raise RuntimeError(f"Expected READY GET, got: {response}")
    if parts[2] != remote_path:
        raise RuntimeError(f"READY GET path mismatch: {parts[2]} (expected {remote_path})")

    try:
        size = int(parts[3])
    except ValueError as exc:
        raise RuntimeError(f"Invalid READY GET size: {parts[3]}") from exc

    payload = bytearray()
    deadline = time.time() + max(timeout_s, 15.0)
    while len(payload) < size:
        if time.time() >= deadline:
            raise TimeoutError(
                f"Timed out while reading GET payload ({len(payload)}/{size} bytes)"
            )

        chunk = port.read(size - len(payload))
        if not chunk:
            continue
        payload.extend(chunk)

    response = read_cdc_line(port, timeout_s)
    if not response.startswith("CDC:OK GET"):
        raise RuntimeError(f"Expected OK GET, got: {response}")

    local_path.write_bytes(payload)
    print(f"Saved {len(payload)} bytes to {local_path}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Test the ScreenDeck CDC protocol")
    parser.add_argument("--port", required=True, help="Serial port (COM5, /dev/ttyACM0, etc.)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=15.0, help="Response timeout in seconds")
    parser.add_argument("--wait", type=float, default=1.0, help="Seconds to wait after opening port")
    parser.add_argument("--chunk-size", type=int, default=2048, help="Binary payload chunk size in bytes")
    parser.add_argument("--retry", type=int, default=2, help="Retries for PUT TIMEOUT responses")
    parser.add_argument(
        "--inter-chunk-ms",
        type=float,
        default=0.0,
        help="Delay between binary chunks in milliseconds",
    )
    parser.add_argument(
        "--chunk-ack",
        action="store_true",
        help="Use acknowledged PUTC uploads instead of streaming PUT",
    )
    parser.add_argument("--ping", action="store_true", help="Send PING command")
    parser.add_argument("--status", action="store_true", help="Send STATUS command")
    parser.add_argument("--ss-status", action="store_true", help="Send SS STATUS command")
    parser.add_argument(
        "--ss-play",
        nargs="?",
        type=int,
        const=1,
        metavar="LOOPS",
        help="Send SS PLAY with an optional loop count",
    )
    parser.add_argument(
        "--command",
        action="append",
        help="Send an arbitrary CDC command (can be repeated)",
    )
    parser.add_argument(
        "--put",
        nargs=2,
        action="append",
        metavar=("LOCAL_FILE", "REMOTE_PATH"),
        help="Upload LOCAL_FILE to REMOTE_PATH (can be repeated)",
    )
    parser.add_argument(
        "--get",
        nargs=2,
        action="append",
        metavar=("REMOTE_PATH", "LOCAL_FILE"),
        help="Download REMOTE_PATH to LOCAL_FILE (can be repeated)",
    )
    parser.add_argument(
        "--reload",
        choices=["MACROS", "ICONS", "ALL"],
        help="Send RELOAD command after uploads",
    )

    args = parser.parse_args()

    if args.chunk_size <= 0:
        raise SystemExit("--chunk-size must be > 0")
    if args.retry < 0:
        raise SystemExit("--retry must be >= 0")
    if args.inter_chunk_ms < 0:
        raise SystemExit("--inter-chunk-ms must be >= 0")

    try:
        with serial.Serial(args.port, args.baud, timeout=0.25) as port:
            time.sleep(args.wait)
            port.reset_input_buffer()

            if args.ping:
                response = send_command(port, "PING", args.timeout)
                if response != "CDC:PONG":
                    raise RuntimeError(f"Expected CDC:PONG, got: {response}")

            if args.status:
                send_command(port, "STATUS", args.timeout)

            if args.put:
                for local_file, remote_path in args.put:
                    upload_file(
                        port,
                        pathlib.Path(local_file),
                        remote_path,
                        args.timeout,
                        args.chunk_size,
                        args.retry,
                        args.inter_chunk_ms,
                        args.chunk_ack,
                    )

            if args.get:
                for remote_path, local_file in args.get:
                    download_file(
                        port,
                        remote_path,
                        pathlib.Path(local_file),
                        args.timeout,
                    )

            if args.reload:
                response = send_command(port, f"RELOAD {args.reload}", args.timeout)
                if not response.startswith("CDC:OK RELOAD"):
                    raise RuntimeError(f"Expected RELOAD OK, got: {response}")

            if args.ss_status:
                send_command(port, "SS STATUS", args.timeout)

            if args.ss_play is not None:
                if args.ss_play <= 0:
                    raise RuntimeError("--ss-play loop count must be > 0")
                send_command(port, f"SS PLAY {args.ss_play}", args.timeout)

            if args.command:
                for command in args.command:
                    send_command(port, command, args.timeout)

            print("Done", flush=True)
            return 0

    except Exception as exc:  # pylint: disable=broad-except
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
