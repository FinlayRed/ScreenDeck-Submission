# Deck Macro Editor (Tauri)

Desktop editor for the Waveshare deck firmware protocol implemented in this repository.

## Features

- 4x8 icon grid matching device layout
- per-slot macro editing (`combo` and `delay`)
- image import + resize to `85x85`
- image conversion to device format (RGB565 little-endian raw bytes)
- sends only changed icon files
- uploads `macros.json` when macro data changed
- executes CDC protocol commands and triggers live reload on device
- auto-probes ports and auto-selects a responsive CDC port

## Requirements

- Node.js 18+
- Rust stable toolchain
- Tauri prerequisites for your OS

## Run (development)

From `tauriapp/`:

```bash
npm install
npm run tauri dev
```

## Build

```bash
npm run tauri build
```

## Usage

1. Open app, click `Refresh Ports`, select the device COM port.
2. Click an icon in the grid.
3. Use `Choose Image` to assign icon art (converted automatically).
4. Add/edit actions in the right panel.
5. Click `Send Changes To Device`.

The app performs:

- `PING`
- `PUT /icon_r_c.bin ...` for changed icons
- `PUT /macros.json ...` if macros changed
- `RELOAD ALL`

## Protocol assumptions

The app expects firmware responses prefixed with `CDC:` and uses the command/PUT flow documented in:

- `tools/cdc_protocol.md`
- `tools/cdc_protocol_spec.md`
