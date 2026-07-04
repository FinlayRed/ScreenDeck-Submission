#!/usr/bin/env python3
import argparse
import pathlib
import shutil
import struct
import subprocess
import zlib


MAGIC = b"SDMJ"
VERSION = 1
DEFAULT_WIDTH = 200
DEFAULT_HEIGHT = 120
DEFAULT_FPS = 15
DEFAULT_DURATION_S = 10.0
DEFAULT_QUALITY = 2

HEADER_STRUCT = struct.Struct("<4sHHHHHHIIIIII")
INDEX_STRUCT = struct.Struct("<II")
HEADER_SIZE = HEADER_STRUCT.size


def run_ffmpeg(
    ffmpeg: str,
    source: pathlib.Path,
    frames_dir: pathlib.Path,
    width: int,
    height: int,
    fps: int,
    duration_s: float,
    quality: int,
) -> list[pathlib.Path]:
    frame_pattern = frames_dir / "frame_%05d.jpg"
    max_frames = max(1, int(duration_s * fps))
    vf = (
        f"fps={fps},"
        f"scale={width}:{height}:force_original_aspect_ratio=decrease,"
        f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:color=black,"
        "format=yuvj420p"
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
        "-q:v",
        str(quality),
        str(frame_pattern),
    ]
    subprocess.run(command, check=True)

    frames = sorted(frames_dir.glob("frame_*.jpg"))
    if not frames:
        raise RuntimeError("ffmpeg produced no JPEG frames")
    return frames


def read_jpeg_frames(frame_paths: list[pathlib.Path]) -> list[bytes]:
    frames: list[bytes] = []
    for frame_path in frame_paths:
        data = frame_path.read_bytes()
        if len(data) < 4 or not data.startswith(b"\xff\xd8") or not data.endswith(b"\xff\xd9"):
            raise RuntimeError(f"{frame_path} is not a complete JPEG frame")
        frames.append(data)
    return frames


def write_sdmj(
    output: pathlib.Path,
    frames: list[bytes],
    width: int,
    height: int,
    fps: int,
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)

    index_offset = HEADER_SIZE
    data_offset = HEADER_SIZE + len(frames) * INDEX_STRUCT.size
    frame_offset = data_offset
    index_entries = bytearray()
    max_frame_size = 0

    for frame in frames:
        index_entries.extend(INDEX_STRUCT.pack(frame_offset, len(frame)))
        frame_offset += len(frame)
        max_frame_size = max(max_frame_size, len(frame))

    payload = b"".join(frames)
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
        0,
        len(frames),
        index_offset,
        data_offset,
        total_size,
        max_frame_size,
        crc,
    )

    output.write_bytes(header + bytes(index_entries) + payload)


def build_sdmj(
    source: pathlib.Path,
    output: pathlib.Path,
    ffmpeg: str = "ffmpeg",
    width: int = DEFAULT_WIDTH,
    height: int = DEFAULT_HEIGHT,
    fps: int = DEFAULT_FPS,
    duration_s: float = DEFAULT_DURATION_S,
    quality: int = DEFAULT_QUALITY,
) -> dict[str, int]:
    if not source.exists():
        raise FileNotFoundError(source)
    if width <= 0 or height <= 0:
        raise ValueError("width and height must be positive")
    if fps <= 0:
        raise ValueError("fps must be positive")
    if duration_s <= 0:
        raise ValueError("duration must be positive")
    if quality < 2 or quality > 31:
        raise ValueError("ffmpeg MJPEG quality must be 2..31")

    output.parent.mkdir(parents=True, exist_ok=True)
    frames_dir = output.parent / f"{output.stem}_frames"
    if frames_dir.exists():
        shutil.rmtree(frames_dir)
    frames_dir.mkdir(parents=True)

    try:
        frame_paths = run_ffmpeg(
            ffmpeg,
            source,
            frames_dir,
            width,
            height,
            fps,
            duration_s,
            quality,
        )
        frames = read_jpeg_frames(frame_paths)
        write_sdmj(output, frames, width, height, fps)
    finally:
        if frames_dir.exists():
            shutil.rmtree(frames_dir)

    return {
        "frames": len(frames),
        "bytes": output.stat().st_size,
        "max_frame": max(len(frame) for frame in frames),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert a video/GIF to ScreenDeck MJPEG")
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    parser.add_argument("--fps", type=int, default=DEFAULT_FPS)
    parser.add_argument("--duration", type=float, default=DEFAULT_DURATION_S)
    parser.add_argument(
        "--quality",
        type=int,
        default=DEFAULT_QUALITY,
        help="ffmpeg MJPEG qscale, 2 is highest quality and 31 is lowest",
    )
    args = parser.parse_args()

    stats = build_sdmj(
        args.input,
        args.output,
        ffmpeg=args.ffmpeg,
        width=args.width,
        height=args.height,
        fps=args.fps,
        duration_s=args.duration,
        quality=args.quality,
    )
    print(
        f"Wrote {args.output} "
        f"({stats['frames']} frames, {stats['bytes']} bytes, max frame {stats['max_frame']} bytes)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
