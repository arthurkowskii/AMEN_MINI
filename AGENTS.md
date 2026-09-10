# Repository Guide

## Source Of Truth

- Active development is on `dev`. The former PSRAM audio draft was retired in September 2026 and remains in Git history. The MIDI firmware (`firmware_midi/`) was restored as the currently flashed instrument firmware: it boots in CHROMATIC preset, NONE page.
- The current target is a Teensy 4.1 SD sample player: 20 assignable pads, four simultaneous sources, WAV PCM16 mono/stereo at 44.1 kHz, no PSRAM dependency.
- Shift + pad opens assignment, E1 browses, its click enters folders or assigns a file, and a new Shift press cancels. Assignments are volatile. Playback is one-shot; retrigger replaces the same pad's voice and a fifth source steals the oldest voice.
- E7 (A=33, B=34) adjusts headphone volume from 0 to 100, starting muted at every boot. This controls the SGTL5000 headphone output, not line-out. Volume writes require a successfully initialized codec.
- `firmware/firmware.ino` is the hardware entrypoint. Portable C++17 belongs under `firmware/src/engine/`; Arduino/Teensy code belongs under `firmware/src/teensy/`.
- `hardware/AMEN_MINI.*` describes the active PCB. `hardware/COMPONENT_HANDOFF.md` describes an obsolete Pico design and must not be used for the active architecture.
- `diagnostics/` is the independent, validated control-surface diagnostic. Root-level C++ files and `compOut/` are learning prototypes.

## Verification

- Use `build_firmware.ps1` for the Teensy build with `teensy:avr` 1.62.0. It accepts `-ArduinoCli` and `-ConfigFile`, builds USB Serial, and places the HEX in ignored `firmware/build/`.
- `arduino-cli` may not be in PATH; the build script also checks the session's temporary installation under `%LOCALAPPDATA%/Temp/opencode/arduino-cli/`.
- Use `start_firmware.ps1` for the Windows listening harness. Keep its source tracking, build options and displayed controls aligned with `firmware/test_native/rt_player.cpp`.
- Native tests: configure `cmake -S firmware -B "$env:LOCALAPPDATA/Temp/opencode/amen-stream-build" -G "MinGW Makefiles"`, build that directory, then run `ctest --test-dir` on it with `--output-on-failure`. `start_firmware.ps1 -Smoke` runs the separate PC audio integration check.
- `firmware/amen_rt.exe` is an intentionally tracked runnable deliverable. Rebuild it in place after harness or shared-engine changes; never restore a stale binary as cleanup. Verify its new controls and leave its modification alongside source changes.
- Keep `firmware/test_native/third_party/miniaudio.h` vendored. Third-party compiler warnings should be distinguished from project warnings.
- Native tests and successful compilation do not validate physical SD latency, codec wiring, OLED operation or four-source playback. Record only measurements actually made on the instrument.

## Real-Time Contracts

- File access, directory scans, allocation and OLED I/O must stay outside the audio callback.
- Audio consumes bounded PCM buffers. Cross-context ownership and publication must be explicit; never mask interrupts around an SD operation.
- Reject invalid/unsupported WAV files before replacing an assignment. Handle short reads, end-of-file and buffer starvation explicitly.
- Keep input scanning consistent with the validated diagnostic: inactive rows high impedance, columns pulled up, 3 us settling and symmetric 5 ms debounce.

## Operational Gotchas

- Do not rewrite `.kicad_sch` or `.kicad_pcb` by script. KiCad lock/session/history files are machine-local and ignored.
- Do not recreate the old PSRAM sampler, harmonic MIDI engine or their roadmaps as part of the new minimal player.
