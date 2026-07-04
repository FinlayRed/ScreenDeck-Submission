# ScreenDeck MJPEG Container

The `.sdmj` container stores one active screensaver at `/screensavers/active.sdmj`.

All integer fields are little-endian. Version 1 uses a fixed 40-byte header, followed by one 8-byte index entry per frame, followed by contiguous JPEG frame bytes.

## Header

| Offset | Type | Name | Value |
| --- | --- | --- | --- |
| 0 | 4 bytes | magic | `SDMJ` |
| 4 | u16 | version | `1` |
| 6 | u16 | header_size | `40` |
| 8 | u16 | width | JPEG source width. Must divide `800`; measured profile uses `200`. |
| 10 | u16 | height | JPEG source height. Must divide `480`; measured profile uses `120`. |
| 12 | u16 | fps | `15` |
| 14 | u16 | flags | `0` |
| 16 | u32 | frame_count | Number of JPEG frames |
| 20 | u32 | index_offset | `40` |
| 24 | u32 | data_offset | First JPEG byte |
| 28 | u32 | total_size | Full file size |
| 32 | u32 | max_frame_size | Largest JPEG frame |
| 36 | u32 | crc32 | CRC over index bytes plus JPEG bytes |

## Index Entry

Each frame has one entry:

| Offset | Type | Name |
| --- | --- | --- |
| 0 | u32 | frame_offset |
| 4 | u32 | frame_size |

Frame offsets are absolute file offsets. Version 1 requires frames to be contiguous and in index order.

## Playback Profile

The firmware bypasses LVGL and decodes each JPEG directly into the LCD framebuffer. If the source dimensions are smaller than `800x480`, the decoder output callback upscales by an integer factor while writing the framebuffer.

Measured on the current board with `lucy gif small01687971.gif`:

- `800x480` MJPEG: decode-bound, around `4 fps`.
- `400x240` MJPEG upscaled 2x: around `12 fps`.
- `200x120` MJPEG upscaled 4x, ffmpeg `q=2`: `14.99 fps`, `0` dropped frames over 110 frames.

The desktop app should pre-render source media to `200x120`, `15 fps`, qscale `2` unless a better profile is measured for a different board.
