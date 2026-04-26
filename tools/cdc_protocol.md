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
- `GET <remote_path>`

Allowed upload paths:
- `/macros.json`
- `/fallback.bin`
- `/icon_<row>_<col>.bin` where row is `0..3` and col is `0..7`

Upload flow:
1. Send `PUT /macros.json 1234\n`
2. Wait for `CDC:READY PUT /macros.json 1234`
3. Send exactly `1234` raw bytes
4. Wait for `CDC:OK PUT /macros.json 1234`

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
python tools/cdc_transfer_test.py --port COM5 --put icon_0_0.bin /icon_0_0.bin --chunk-size 128 --retry 3 --inter-chunk-ms 1.0 --reload ICONS
python tools/cdc_transfer_test.py --port COM5 --get /macros.json downloaded_macros.json
python tools/cdc_transfer_test.py --port COM5 --get /icon_0_0.bin icon_0_0_from_device.bin

# Convert PNG -> 85x85 RGB565 LE .bin
python tools/png_to_rgb565.py --in icon.png --out icon_0_0.bin
```
