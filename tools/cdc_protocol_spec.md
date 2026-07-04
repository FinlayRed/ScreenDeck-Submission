# CDC Protocol Specification (Detailed)

This document defines the current firmware CDC control protocol used for live macro/icon updates.

It matches the implementation in `src/main.cpp` for this session.

## 1) Purpose

The protocol allows a desktop app (or test script) to:

- query device status,
- trigger live reloads,
- upload `macros.json`, home icon assets, radial icon assets, and the active screensaver to SD,
- download `macros.json`, home icon assets, radial icon assets, and the active screensaver from SD,
- apply changes without reboot.

## 2) Transport and USB mode

- Transport: USB CDC serial stream.
- Device USB mode: composite device (CDC + HID keyboard).
- Build flags expected:
  - `-DARDUINO_USB_MODE=1`
  - `-DARDUINO_USB_CDC_ON_BOOT=1`

Notes:

- Host COM port may change after enumeration/re-enumeration.
- Firmware may mirror CDC responses to debug serial output for visibility.

## 3) Framing and encoding

### 3.1 Command framing

- Commands are line-based text.
- Command terminator: `\n`.
- `\r` is ignored.
- Parsing is case-insensitive for command keywords and reload targets.

### 3.2 Upload framing

- `PUT` begins an unacknowledged binary data phase.
- `PUTC` begins an acknowledged chunked binary data phase.
- After `CDC:READY ...`, host must send exactly `<size>` raw bytes.
- No checksum or delimiter is currently required after binary payload.

## 4) Command set

## 4.1 `PING`

Request:

```text
PING\n
```

Response:

```text
CDC:PONG
```

## 4.2 `HELP`

Response format:

```text
CDC:CMDS PING|STATUS|SS STATUS|SS PLAY [loops]|RELOAD <MACROS|ICONS|ALL>|PUT <path> <size>|PUTC <path> <size>|GET <path>
```

## 4.3 `STATUS`

Response format:

```text
CDC:STATUS sd=<0|1> usb=<0|1> macros=<int> radial=<int> active=<0|1> events=1 proto=5
```

Field meaning:

- `sd`: SD initialized.
- `usb`: USB keyboard stack initialized.
- `macros`: number of icon slots with non-empty action lists.
- `radial`: number of configured radial menu items.
- `active`: macro executor currently running an action sequence.

## 4.4 `RELOAD <target>`

Accepted targets:

- `MACROS`
- `ICONS`
- `ALL`

Responses:

- `CDC:OK RELOAD MACROS`
- `CDC:OK RELOAD ICONS`
- `CDC:OK RELOAD ALL`

Errors:

- `CDC:ERR RELOAD_TARGET`

## 4.5 `PUT <path> <size>`

Syntax:

```text
PUT /macros.json 1234
```

When accepted:

```text
CDC:READY PUT /macros.json 1234
```

After receiving exactly `size` bytes:

```text
CDC:OK PUT /macros.json 1234
```

Then firmware applies updates:

- `/macros.json` -> reloads macro config and emits `CDC:INFO RELOAD MACROS`
- icon/fallback/radial asset -> stores the file and emits `CDC:INFO ICON STORED`; send `RELOAD ICONS` or `RELOAD ALL` to rebuild the grid

## 4.6 `PUTC <path> <size>`

`PUTC` is the recommended upload command for screensavers and other large files. It uses the same target paths and temp-file semantics as `PUT`, but flow-controls the transfer.

Syntax:

```text
PUTC /screensavers/active.sdmj 1234
```

When accepted:

```text
CDC:READY PUTC /screensavers/active.sdmj 1234
```

For each chunk, host sends:

```text
uint16_le chunk_size
<chunk_size raw bytes>
```

After each non-final chunk:

```text
CDC:ACK PUTC <received_bytes>
```

After receiving exactly `size` bytes:

```text
CDC:OK PUTC /screensavers/active.sdmj 1234
```

## 4.7 `GET <path>`

Syntax:

```text
GET /macros.json
```

When accepted:

```text
CDC:READY GET /macros.json 1234
```

Firmware then streams exactly `size` raw bytes, followed by:

```text
CDC:OK GET /macros.json 1234
```

Missing file:

```text
CDC:ERR GET NOT_FOUND
```

## 5) Allowed upload targets

Only these paths are accepted:

- `/macros.json`
- `/fallback.bin`
- `/icon_<row>_<col>.bin` where `row` is `0..3`, `col` is `0..7`
- `/radial_<row>_<col>_<direction>.bin` where `direction` is `n`, `e`, `s`, or `w`
- `/screensavers/active.sdmj`
- `/screensavers/active.sdra`

Anything else returns:

```text
CDC:ERR PATH_NOT_ALLOWED
```

## 6) Size and timeout limits

- Upload size range: `1..67108864` bytes (`64 MB`).
- Upload idle timeout: `30000 ms`.
- Command line buffer length: 159 chars usable (160 with null terminator).

Errors:

- `CDC:ERR INVALID_SIZE`
- `CDC:ERR PUT TIMEOUT`
- `CDC:ERR LINE_TOO_LONG`

## 7) Upload state machine

States:

1. `Idle`
2. `Uploading`

Transitions:

- `Idle` + valid `PUT` or `PUTC` -> open temp file, emit `CDC:READY...`, enter `Uploading`.
- `Uploading` + receive bytes until expected count -> close + rename, emit `CDC:OK...`, auto-apply, return to `Idle`.
- `Uploading` + timeout/write/rename failure -> emit `CDC:ERR PUT ...` or `CDC:ERR PUTC ...`, cleanup temp, return to `Idle`.

Important behavior:

- While in `Uploading`, all incoming bytes are treated as payload/chunk framing (not commands).
- A second `PUT` or `PUTC` during active upload returns:

```text
CDC:ERR UPLOAD_ALREADY_ACTIVE
```

## 8) File write semantics

Writes are atomic-ish at file level:

1. Open `<target>.tmp`.
2. Stream payload into temp file.
3. On success, remove existing `<target>` (if present).
4. Rename temp file to `<target>`.

Failures:

- `CDC:ERR OPEN_TEMP_FAILED`
- `CDC:ERR PUT WRITE_FAILED`
- `CDC:ERR PUT RENAME_FAILED`

On failure, temp file is deleted.

## 9) Error responses (current set)

- `CDC:ERR UNKNOWN_CMD`
- `CDC:ERR RELOAD_TARGET`
- `CDC:ERR PUT_SYNTAX`
- `CDC:ERR PUTC_SYNTAX`
- `CDC:ERR SD_NOT_READY`
- `CDC:ERR UPLOAD_ALREADY_ACTIVE`
- `CDC:ERR PATH_NOT_ALLOWED`
- `CDC:ERR INVALID_SIZE`
- `CDC:ERR OPEN_TEMP_FAILED`
- `CDC:ERR LINE_TOO_LONG`
- `CDC:ERR PUT TIMEOUT`
- `CDC:ERR PUT WRITE_FAILED`
- `CDC:ERR PUT RENAME_FAILED`
- `CDC:ERR PUTC TIMEOUT`
- `CDC:ERR PUTC BAD_CHUNK`
- `CDC:ERR PUTC WRITE_FAILED`
- `CDC:ERR PUTC RENAME_FAILED`
- `CDC:ERR GET_SYNTAX`
- `CDC:ERR GET NOT_FOUND`
- `CDC:ERR GET OPEN_FAILED`
- `CDC:ERR GET INVALID_SIZE`
- `CDC:ERR GET IO_FAILED`

## 10) Host-side implementation guidance

- Always wait for `CDC:READY ...` before sending binary payload.
- Send exactly `size` bytes, no more/no less.
- Use `PUTC` for screensavers and other large files; use `PUT` only for small files or compatibility with older firmware.
- Treat responses as line-oriented UTF-8 text.
- Re-open/re-scan COM port if USB re-enumerates.
- Close other serial monitors before transfer.

Recommended upload sequence for batch updates:

1. `PUTC` each changed large file/screensaver, or `PUT` small compatibility files.
2. Wait for each `CDC:OK PUTC ...` or `CDC:OK PUT ...`.
3. Optionally send `RELOAD ALL` when done.

## 11) Protocol grammar (informal)

```text
command     = ping / help / status / ss_status / ss_play / reload / put / putc / get
ping        = "PING"
help        = "HELP"
status      = "STATUS"
ss_status   = "SS" SP "STATUS"
ss_play     = "SS" SP "PLAY" [ SP loops ]
reload      = "RELOAD" SP target
target      = "MACROS" / "ICONS" / "ALL"
put         = "PUT" SP path SP size
putc        = "PUTC" SP path SP size
get         = "GET" SP path
path        = "/macros.json" / "/fallback.bin" / "/screensavers/active.sdmj" / "/screensavers/active.sdra" / "/icon_" row "_" col ".bin" / "/radial_" row "_" col "_" direction ".bin"
row         = "0" / "1" / "2" / "3"
col         = "0" / "1" / "2" / "3" / "4" / "5" / "6" / "7"
direction   = "n" / "e" / "s" / "w"
size        = 1*DIGIT ; decimal integer 1..67108864
loops       = 1*DIGIT ; decimal integer 1..1000
```

## 12) Screensaver status

`SS STATUS` validates `/screensavers/active.sdmj` when present. If it is missing, firmware falls back to `/screensavers/active.sdra`.

Success:

```text
CDC:SS STATUS ok=1 format=SDMJ file=/screensavers/active.sdmj bytes=123456 frames=150 width=200 height=120 fps=15 max_frame=1234 data=120000 crc=89ABCDEF
```

Failure:

```text
CDC:SS STATUS ok=0 file=/screensavers/active.sdmj err=NOT_FOUND
```

The `.sdmj` format is documented in `tools/sdmj_format.md`. The firmware accepts SDMJ frames whose dimensions divide evenly into the `800x480` framebuffer and upscales them directly. The measured profile for this board is `200x120`, `15 fps`, `q=2`.

## 13) Screensaver playback benchmark

`SS PLAY [loops]` plays `/screensavers/active.sdmj` through the direct LCD framebuffer path. `loops` defaults to `1`.

Success:

```text
CDC:SS PLAY ok=1 format=SDMJ target=15 frames=110 loops=5 fps=14.99 raw_fps=16.70 dropped=0 avg_read_us=5773 avg_decode_us=54057 avg_switch_us=26 max_frame_us=62250 total_ms=7336 touch=0
```

Failure:

```text
CDC:SS PLAY ok=0 err=JPEG_DEC_6 frames=3
```

Fields:

- `fps`: paced playback rate including frame delays.
- `raw_fps`: throughput without pacing delay, based on read + decode + switch time.
- `dropped`: frames whose read/decode/switch work exceeded the target frame interval.

## 14) Macro config radial schema

`macros.json` version `2` adds optional radial menu config. Firmware still accepts version `1` files.

```json
{
  "version": 2,
  "icons": [
    {
      "index": 0,
      "row": 0,
      "col": 0,
      "actions": [],
      "hostActions": [],
      "radial": {
        "enabled": true,
        "items": [
          {
            "direction": "n",
            "enabled": true,
            "actions": [{ "type": "combo", "key": "A", "mods": [] }],
            "hostActions": []
          }
        ]
      }
    }
  ]
}
```

Touch events emitted for companion actions:

```text
CDC:EVENT BUTTON <index> <row> <col>
CDC:EVENT RADIAL <index> <row> <col> <direction>
```

## 15) Current non-goals / known gaps

- No authentication/authorization.
- No payload checksum/CRC.
- No resumable upload.
- No multipart/batch transaction command.
- No explicit protocol version handshake.

## 16) Example session

```text
> PING
< CDC:PONG

> STATUS
< CDC:STATUS sd=1 usb=1 macros=5 radial=2 active=0 events=1 proto=5

> PUT /icon_0_0.bin 14450
< CDC:READY PUT /icon_0_0.bin 14450
> [send 14450 raw bytes]
< CDC:OK PUT /icon_0_0.bin 14450
< CDC:INFO RELOAD ICONS

> PUT /macros.json 980
< CDC:READY PUT /macros.json 980
> [send 980 raw bytes]
< CDC:OK PUT /macros.json 980
< CDC:INFO RELOAD MACROS
```
