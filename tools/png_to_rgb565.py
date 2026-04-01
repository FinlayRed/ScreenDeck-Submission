#!/usr/bin/env python3
import argparse
import importlib
from pathlib import Path


def get_image_module():
    try:
        image_module = importlib.import_module("PIL.Image")
    except Exception as exc:  # pylint: disable=broad-except
        raise SystemExit("Pillow is required. Install with: pip install pillow") from exc

    return image_module


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert an image to 85x85 RGB565 LE raw bytes for device icons"
    )
    parser.add_argument("--in", dest="input_path", required=True, help="Input image path (png/jpg/etc)")
    parser.add_argument("--out", dest="output_path", required=True, help="Output .bin path")
    parser.add_argument("--size", type=int, default=85, help="Output width/height in pixels")
    parser.add_argument(
        "--resample",
        choices=["nearest", "bilinear", "bicubic", "lanczos"],
        default="lanczos",
        help="Resampling algorithm",
    )
    return parser.parse_args()


def resample_mode(image_module, name: str) -> int:
    if name == "nearest":
        return image_module.Resampling.NEAREST
    if name == "bilinear":
        return image_module.Resampling.BILINEAR
    if name == "bicubic":
        return image_module.Resampling.BICUBIC
    return image_module.Resampling.LANCZOS


def convert_to_rgb565_le(input_path: Path, output_path: Path, size: int, resample: str) -> int:
    image_module = get_image_module()

    image = image_module.open(input_path).convert("RGB")
    image = image.resize((size, size), resample_mode(image_module, resample))

    pixels = image.load()
    payload = bytearray(size * size * 2)

    idx = 0
    for y in range(size):
        for x in range(size):
            red, green, blue = pixels[x, y]
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            payload[idx] = value & 0xFF
            payload[idx + 1] = (value >> 8) & 0xFF
            idx += 2

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(payload)
    return len(payload)


def main() -> int:
    args = parse_args()

    if args.size <= 0:
        raise SystemExit("--size must be > 0")

    input_path = Path(args.input_path)
    output_path = Path(args.output_path)

    if not input_path.exists():
        raise SystemExit(f"Input file not found: {input_path}")

    size = convert_to_rgb565_le(input_path, output_path, args.size, args.resample)
    print(f"Wrote {output_path} ({size} bytes)")
    print(f"Expected icon size for 85x85 is {85 * 85 * 2} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
