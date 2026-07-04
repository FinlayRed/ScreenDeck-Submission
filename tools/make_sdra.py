#!/usr/bin/env python3
import argparse
import pathlib
import shutil
import struct
import subprocess
import zlib


MAGIC = b"SDRA"
VERSION = 1
DEFAULT_WIDTH = 800
DEFAULT_HEIGHT = 480
DEFAULT_FPS = 15
DEFAULT_DURATION_S = 10.0
DEFAULT_TILE_SIZE = 16

HEADER_STRUCT = struct.Struct("<4sHHHHHHIIIIII")
INDEX_STRUCT = struct.Struct("<II")
FRAME_HEADER_STRUCT = struct.Struct("<HH")
TILE_INDEX_STRUCT = struct.Struct("<H")
HEADER_SIZE = HEADER_STRUCT.size
FRAME_FLAG_FULL = 1


def run_ffmpeg(
    ffmpeg: str,
    source: pathlib.Path,
    raw_output: pathlib.Path,
    width: int,
    height: int,
    fps: int,
    duration_s: float,
) -> None:
    max_frames = max(1, int(duration_s * fps))
    vf = (
        f"fps={fps},"
        f"scale={width}:{height}:force_original_aspect_ratio=decrease,"
        f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color=black,"
        "format=rgb565le"
    )

    command = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-t",
        str(duration_s),
        "-i",
        str(source),
        "-an",
        "-vf",
        vf,
        "-frames:v",
        str(max_frames),
        "-f",
        "rawvideo",
        str(raw_output),
    ]
    subprocess.run(command, check=True)


def read_rgb565_frames(raw_path: pathlib.Path, width: int, height: int) -> list[bytes]:
    frame_size = width * height * 2
    data = raw_path.read_bytes()
    if len(data) < frame_size or len(data) % frame_size != 0:
        raise RuntimeError(
            f"raw stream size {len(data)} is not a positive multiple of {frame_size}"
        )
    return [data[offset : offset + frame_size] for offset in range(0, len(data), frame_size)]


def tile_changed(
    previous: bytes,
    current: bytes,
    width: int,
    tile_x: int,
    tile_y: int,
    tile_size: int,
) -> bool:
    row_bytes = width * 2
    tile_bytes = tile_size * 2
    row_start = (tile_y * tile_size * width + tile_x * tile_size) * 2
    for row in range(tile_size):
        start = row_start + row * row_bytes
        end = start + tile_bytes
        if previous[start:end] != current[start:end]:
            return True
    return False


def append_tile(
    output: bytearray,
    frame: bytes,
    width: int,
    tile_x: int,
    tile_y: int,
    tile_size: int,
) -> None:
    row_bytes = width * 2
    tile_bytes = tile_size * 2
    row_start = (tile_y * tile_size * width + tile_x * tile_size) * 2
    tile_index = tile_y * (width // tile_size) + tile_x
    output.extend(TILE_INDEX_STRUCT.pack(tile_index))
    for row in range(tile_size):
        start = row_start + row * row_bytes
        output.extend(frame[start : start + tile_bytes])


def encode_delta_frame(
    previous: bytes,
    current: bytes,
    width: int,
    height: int,
    tile_size: int,
) -> bytes:
    tiles_x = width // tile_size
    tiles_y = height // tile_size
    tile_count = 0
    payload = bytearray()
    payload.extend(FRAME_HEADER_STRUCT.pack(0, 0))

    for tile_y in range(tiles_y):
        for tile_x in range(tiles_x):
            if tile_changed(previous, current, width, tile_x, tile_y, tile_size):
                append_tile(payload, current, width, tile_x, tile_y, tile_size)
                tile_count += 1

    payload[: FRAME_HEADER_STRUCT.size] = FRAME_HEADER_STRUCT.pack(0, tile_count)
    return bytes(payload)


def write_sdra(
    output: pathlib.Path,
    frames: list[bytes],
    width: int,
    height: int,
    fps: int,
    tile_size: int,
) -> dict[str, int]:
    output.parent.mkdir(parents=True, exist_ok=True)

    full_frame_size = width * height * 2
    encoded_frames: list[bytes] = [
        FRAME_HEADER_STRUCT.pack(FRAME_FLAG_FULL, 0) + frames[0]
    ]
    max_delta_tiles = 0
    total_delta_tiles = 0

    for previous, current in zip(frames, frames[1:]):
        encoded = encode_delta_frame(previous, current, width, height, tile_size)
        tile_count = FRAME_HEADER_STRUCT.unpack(encoded[: FRAME_HEADER_STRUCT.size])[1]
        delta_size = len(encoded)
        full_size = FRAME_HEADER_STRUCT.size + full_frame_size
        if delta_size >= full_size:
            encoded = FRAME_HEADER_STRUCT.pack(FRAME_FLAG_FULL, 0) + current
            tile_count = width * height // (tile_size * tile_size)

        encoded_frames.append(encoded)
        max_delta_tiles = max(max_delta_tiles, tile_count)
        total_delta_tiles += tile_count

    index_offset = HEADER_SIZE
    data_offset = HEADER_SIZE + len(encoded_frames) * INDEX_STRUCT.size
    frame_offset = data_offset
    index_entries = bytearray()
    max_frame_size = 0

    for encoded in encoded_frames:
        index_entries.extend(INDEX_STRUCT.pack(frame_offset, len(encoded)))
        frame_offset += len(encoded)
        max_frame_size = max(max_frame_size, len(encoded))

    payload = b"".join(encoded_frames)
    total_size = data_offset + len(payload)
    crc = zlib.crc32(index_entries)
    crc = zlib.crc32(payload, crc) & 0xFFFFFFFF

    header = HEADER_STRUCT.pack(
        MAGIC,
        VERSION,
        HEADER_SIZE,
        width,
        height,
        fps,
        tile_size,
        len(encoded_frames),
        index_offset,
        data_offset,
        total_size,
        max_frame_size,
        crc,
    )

    output.write_bytes(header + bytes(index_entries) + payload)
    return {
        "frames": len(encoded_frames),
        "bytes": output.stat().st_size,
        "max_frame": max_frame_size,
        "max_delta_tiles": max_delta_tiles,
        "avg_delta_tiles": total_delta_tiles // max(1, len(encoded_frames) - 1),
    }


def build_sdra(
    source: pathlib.Path,
    output: pathlib.Path,
    ffmpeg: str = "ffmpeg",
    width: int = DEFAULT_WIDTH,
    height: int = DEFAULT_HEIGHT,
    fps: int = DEFAULT_FPS,
    duration_s: float = DEFAULT_DURATION_S,
    tile_size: int = DEFAULT_TILE_SIZE,
) -> dict[str, int]:
    if not source.exists():
        raise FileNotFoundError(source)
    if width <= 0 or height <= 0:
        raise ValueError("width and height must be positive")
    if fps <= 0:
        raise ValueError("fps must be positive")
    if duration_s <= 0:
        raise ValueError("duration must be positive")
    if tile_size <= 0 or width % tile_size != 0 or height % tile_size != 0:
        raise ValueError("tile size must evenly divide width and height")

    output.parent.mkdir(parents=True, exist_ok=True)
    raw_path = output.parent / f"{output.stem}.rgb565"
    if raw_path.exists():
        raw_path.unlink()

    try:
        run_ffmpeg(ffmpeg, source, raw_path, width, height, fps, duration_s)
        frames = read_rgb565_frames(raw_path, width, height)
        return write_sdra(output, frames, width, height, fps, tile_size)
    finally:
        if raw_path.exists():
            raw_path.unlink()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert a video/GIF to ScreenDeck raw RGB565 delta animation"
    )
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    parser.add_argument("--fps", type=int, default=DEFAULT_FPS)
    parser.add_argument("--duration", type=float, default=DEFAULT_DURATION_S)
    parser.add_argument("--tile-size", type=int, default=DEFAULT_TILE_SIZE)
    args = parser.parse_args()

    stats = build_sdra(
        args.input,
        args.output,
        ffmpeg=args.ffmpeg,
        width=args.width,
        height=args.height,
        fps=args.fps,
        duration_s=args.duration,
        tile_size=args.tile_size,
    )
    print(
        f"Wrote {args.output} "
        f"({stats['frames']} frames, {stats['bytes']} bytes, "
        f"max frame {stats['max_frame']} bytes, "
        f"max delta tiles {stats['max_delta_tiles']}, "
        f"avg delta tiles {stats['avg_delta_tiles']})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
