# Repository Guide

## Source Of Truth

- Active development is on `dev`, not the default `main` branch; `firmware/docs/ROADMAP.md` records the current milestones and verification criteria.
- The current `dev` hardware and firmware target is AMEN_MINI on Teensy 4.1. Root `README.md` and `hardware/COMPONENT_HANDOFF.md` still describe an older AKOR_01/Pico design and contradict the actual `hardware/AMEN_MINI.*` files; do not use them to infer the active architecture.
- `firmware/src/engine/` is the portable C++17 audio engine. It must remain free of Arduino/Teensy includes; hardware integration belongs under the future `firmware/src/teensy/` layer.
- Root-level C++ files and `compOut/` are standalone learning prototypes, not firmware entrypoints.
- There is currently no build system, `firmware.ino`, or `src/teensy/`; Arduino commands in the roadmap describe future work, not a verification step that works today.

## Native Verification

Run native commands from `firmware/`.

- Build the WAV loader test with warnings enabled (PowerShell): `g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -I src/engine test_native/main.cpp src/engine/wav_loader.cpp -o "$env:TEMP/amen_test.exe"`.
- The loader test writes `out.wav` in its working directory. Run it from a temporary directory if you do not want an artifact in the repo.
- Full format validation is `python3 test_native/check_formats.py`, but the script hardcodes its test executable as `/tmp/amen_test`; it is directly usable on Linux after compiling to that path, not from ordinary Windows Python. It also regenerates the committed files under `test_native/test_wavs/`.
- Build the Windows listening harness: `g++ -std=c++17 -O2 test_native/rt_player.cpp test_native/sample_catalog_scanner.cpp test_native/screen_preview.cpp src/browser/sample_catalog.cpp src/engine/wav_loader.cpp src/engine/sample_player.cpp src/engine/voice_manager.cpp src/engine/fx/live_repeat.cpp src/ui/screen_ui.cpp -I src/browser -I src/engine -I src/ui -I test_native -I test_native/third_party -o amen_rt.exe -lole32 -lwinmm -lgdi32 -luser32`.
- Run `amen_rt.exe test_native/test.wav`. The keyboard simulates the front panel: numpad 1-6 = voice pads (press = trigger the break, hold = SD browser opens), numpad 7-9 = FX pads (hold = activate the assigned FX), F1-F7 select the active encoder, arrows turn the selected encoder, Enter clicks it, Space retriggers the last played pad, Backspace moves to the parent folder, and `q` exits. E1 browses/assigns, E2 = Repeat dry/wet amount, E3 = Repeat division (1/4, 1/8, 1/16, 1/32), E4 = sample speed, E5 = mode, E6 = reserved, E7 = BPM. The assignable list is BLANK, REPEAT, REVERSE, TRANCE GATE; only Repeat has DSP currently. See `firmware/docs/CONTROLS.md`. Adding strict warnings to this target currently emits warnings from vendored `miniaudio.h`, unlike the engine-only tests.
- After changing listening-harness sources, dependencies, compiler/linker options, or controls, update `start_firmware.ps1` when necessary and run it as the integration check. Its source tracking, build command, and displayed controls must remain aligned with the manual command above.
- `firmware/amen_rt.exe` is an intentionally tracked runnable deliverable, not a disposable build artifact. After changing any listening-harness source, rebuild it in place and leave the updated executable in the worktree. Never restore, discard, or replace it with the previous Git version as cleanup: doing so can give the stale binary a newer timestamp than its sources, causing `start_firmware.ps1` to report `deja compile` and launch old behavior. Before finishing, verify the executable contains or exhibits the new controls and that `git status` shows `firmware/amen_rt.exe` modified alongside the relevant sources.
- `arduino-cli` is not currently installed in the repository's Windows development environment.

## Engine Contracts

- `WavData.samples` is interleaved signed `int16_t`; WAV conversion happens once in `wav_load()`. Real-time `render()` outputs separate float channels in `[-1, 1]`.
- `SamplePlayer::setSample()` stores a pointer rather than copying samples, so the supplied `WavData` must outlive the player.
- Keep `firmware/test_native/third_party/miniaudio.h` vendored; it is the header-only backend for the PC listening harness.

## Operational Gotchas

- Do not rewrite `.kicad_sch` or `.kicad_pcb` by script. KiCad lock, session, and `hardware/.history/` files are machine-local and ignored.
- `firmware/scripts/notion_roadmap.py` is not portable: it contains absolute Linux paths and replaces the existing Notion page contents. Do not run it as a routine local verification command.
