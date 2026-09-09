# AMEN_MINI

AMEN_MINI is a standalone sample player for sound design: assign WAV files from the native microSD card to 20 pads and play up to four sources simultaneously.

## The PCB

### Blueprints (KiCad)

![AMEN_MINI PCB — blueprint 1/2](images/pcb/blueprint_01.png)

![AMEN_MINI PCB — blueprint 2/2](images/pcb/blueprint_02.png)

### The assembled board

![AMEN_MINI PCB — real photo 1/2](images/pcb/real_01.jpg)

![AMEN_MINI PCB — real photo 2/2](images/pcb/real_02.jpg)

The board is a 2-layer, 1.6 mm PCB hosting a socketed Teensy 4.1, with the PJRC Audio Adapter (SGTL5000) plugged into J3/J4. Ordering and assembly details live in `hardware/BOM_AMEN_MINI.csv`.

## Hardware

- **Teensy 4.1** (Cortex-M7, 600 MHz) — control processor, native microSD and USB; no PSRAM required;
- **PJRC Audio Adapter SGTL5000** — headphone + line output;
- 21 MX-compatible switches: 20 source pads + Shift;
- 7 EC11 push encoders;
- SSD1306 I²C OLED, 0.91″, 128 × 32;
- WAV PCM 16-bit / 44.1 kHz, mono or stereo, streamed from SD through fixed RAM buffers.

## Firmware

- `firmware/firmware.ino` — Teensy entrypoint;
- `firmware/src/engine/` — portable C++17 WAV parser and four-voice streaming mixer;
- `firmware/src/teensy/` — SD, I2S/SGTL5000, controls and OLED integration;
- `firmware/test_native/` — native verification and Windows listening harness;
- `build_firmware.ps1` — Teensy build; `start_firmware.ps1` — PC listening harness.

Shift + pad opens assignment. Turn E1 to browse; click E1 to enter a folder or assign a WAV. Press Shift again to cancel. E7 controls headphone volume, displayed on the OLED and reset to mute at startup. Pads play one-shots; assignments are volatile. Crop, effects and MIDI are outside this initial firmware.

## Status

- **Hardware**: PCB fabricated and photographed (above). The Teensy 4.1 + Audio Adapter build is the current target.
- **Firmware**: active development on `dev`. The previous MIDI and audio drafts have been replaced; they remain in Git history. Native tests and the Teensy build pass. The new firmware boots on the connected Teensy with SD working; the SGTL5000 never acknowledges (retried at 100/50/10 kHz on 0x0A/0x2A), consecutive bus scans minutes apart report different phantom addresses with only 0x3C stable, and OLED writes have started failing too. The fault is electrical (shield seating/power or shared I2C wiring), not fixable in software. Hardware playback, latency and polyphony remain unverified.

## License

The repository is licensed under **CC BY-NC 4.0** — see [LICENSE](LICENSE) for the full legal text.

In plain words:

- **Anyone can use, build, modify, and share AMEN_MINI** — for personal or any **non-commercial** use.
- **Commercial use is not allowed** (no selling units, no commercial productions) without prior written permission from the author.
- **Credit**: if you publish content *made with* AMEN_MINI on the internet (music, video, images), credit **Arthur Kowskii Croquebois** (kowskii.com). For personal or offline use, no credit is required.

Made by [Arthur Kowskii Croquebois](https://kowskii.com) — sound designer & game developer.
