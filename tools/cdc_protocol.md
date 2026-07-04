CDC command protocol (device replies always start with `CDC:`)

For the full detailed specification, see `tools/cdc_protocol_spec.md`.

The firmware now uses a composite USB device (CDC + HID). During boot, the USB device may re-enumerate and the COM port number can change.
If monitor/script cannot connect, unplug/replug once and select the new COM port.

Commands:
- `PING`
- `STATUS`
- `HELP`
- `RELOAD MACROS`
- `RELOAD ICONS`
- `RELOAD ALL`
- `PUT <remote_path> <size>`
- `PUTC <remote_path> <size>`
- `GET <remote_path>`
- `SS STATUS`
- `SS PLAY [loops]`

Allowed upload paths:
- `/macros.json`
- `/fallback.bin`
- `/icon_<row>_<col>.bin` where row is `0..3` and col is `0..7`
- `/radial_<row>_<col>_<direction>.bin` where direction is `n`, `e`, `s`, or `w`
- `/screensavers/active.sdmj`
- `/screensavers/active.sdra`

Screensaver:
- Preferred active file: `/screensavers/active.sdmj`; `/screensavers/active.sdra` is a raw diagnostic fallback.
- Format: `tools/sdmj_format.md`
- Performance target: `15 fps` through the direct framebuffer path, bypassing LVGL.
- Recommended SDMJ profile for this board: `200x120`, `15 fps`, `q=2`, upscaled 4x to `800x480`.
- `SS PLAY [loops]` plays the active SDMJ through the direct framebuffer path and reports timing.

Macro config:
- `macros.json` version `2` adds optional per-icon radial menus.
- Version `1` files are still accepted.
- A radial item can contain normal `actions` and optional desktop `hostActions`.

```json
{
  "radial": {
    "enabled": true,
    "items": [
      { "direction": "n", "enabled": true, "actions": [{ "type": "combo", "key": "A", "mods": [] }], "hostActions": [] }
    ]
  }
}
```

Events:
- Main icon press: `CDC:EVENT BUTTON <index> <row> <col>`
- Radial release: `CDC:EVENT RADIAL <index> <row> <col> <direction>`

Upload flow:
1. Send `PUT /macros.json 1234\n`
2. Wait for `CDC:READY PUT /macros.json 1234`
3. Send exactly `1234` raw bytes
4. Wait for `CDC:OK PUT /macros.json 1234`

Acked upload flow for screensavers or large files:
1. Send `PUTC /screensavers/active.sdmj 1234\n`
2. Wait for `CDC:READY PUTC /screensavers/active.sdmj 1234`
3. For each chunk, send `uint16_le chunk_size` followed by that many bytes.
4. Wait for `CDC:ACK PUTC <received_bytes>` after each non-final chunk.
5. Wait for `CDC:OK PUTC /screensavers/active.sdmj 1234` after the final chunk.

Download flow:
1. Send `GET /macros.json\n`
2. Wait for `CDC:READY GET /macros.json 1234`
3. Read exactly `1234` raw bytes
4. Wait for `CDC:OK GET /macros.json 1234`

If a file is missing, firmware returns:
- `CDC:ERR GET NOT_FOUND`

The firmware applies updates like this:
- `/macros.json` upload -> reloads macro config
- icon or fallback upload -> stores the file; send `RELOAD ICONS` or `RELOAD ALL` to rebuild the grid

Quick test script:

```bash
pip install pyserial
python tools/cdc_transfer_test.py --port COM5 --ping --status
python tools/cdc_transfer_test.py --port COM5 --put macros.json /macros.json
python tools/cdc_transfer_test.py --port COM5 --put icon_0_0.bin /icon_0_0.bin --reload ICONS
python tools/cdc_transfer_test.py --port COM5 --put radial_0_0_n.bin /radial_0_0_n.bin --reload ICONS
python tools/cdc_transfer_test.py --port COM5 --put icon_0_0.bin /icon_0_0.bin --chunk-size 128 --retry 3 --inter-chunk-ms 1.0 --reload ICONS
python tools/cdc_transfer_test.py --port COM5 --get /macros.json downloaded_macros.json
python tools/cdc_transfer_test.py --port COM5 --get /icon_0_0.bin icon_0_0_from_device.bin
python tools/make_sdmj.py --input "lucy gif small01687971.gif" --output build/screensaver/active.sdmj --width 200 --height 120 --quality 2
python tools/cdc_transfer_test.py --port COM5 --put build/screensaver/active.sdmj /screensavers/active.sdmj --chunk-ack --ss-status
python tools/cdc_transfer_test.py --port COM5 --ss-play 1

# Convert PNG -> 85x85 RGB565 LE .bin
python tools/png_to_rgb565.py --in icon.png --out icon_0_0.bin
```
