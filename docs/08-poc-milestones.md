# PoC milestones & acceptance

The proof-of-concept goal: open a file into a deck, play/pause, and show deck
state + timecode in Flutter.

## Milestones

1. **Engine builds headless, standalone** — `cmake --preset windows-msvc &&
   cmake --build build-windows --target lazerdeck_engine` produces
   `lazerdeck_engine.dll` with **no** SDL/Qt/vcpkg and **no** VST3.  ✅ done
2. **FFI surface** — `init/shutdown/get_deck_count/get_sample_rate/
   get_deck_state/load_file/play/pause/push_command` all exported.  ✅ verified
   via `dumpbin /exports`.
3. **Flutter app builds & binds** — `flutter build windows --debug` compiles the
   engine via `add_subdirectory`, bundles the DLL, and `flutter analyze` is
   clean.
4. **Runtime PoC** — `flutter run -d windows`: open a WAV, Play → audio out +
   timecode advances, Pause → freezes; two decks independent.
5. **VST3 absence is graceful** — with no `lazerdeck_vst3.dll`, init logs
   "VST support disabled" and audio still plays.

## Manual acceptance script (runtime)

1. `cd app && flutter run -d windows`.
2. App opens with two deck cards (DECK A / DECK B), timecode `00:00.00`.
3. On DECK A click **Open**, pick a `.wav`/`.mp3`. Card shows the filename;
   `LOADING` chip briefly, then BPM populates after analysis.
4. Click **Play** (A): you hear audio; timecode counts up in real time.
5. Click **Pause** (A): audio stops; timecode freezes.
6. Repeat on DECK B while A plays — they advance independently.
7. Close the window: app calls `lazerdeck_shutdown` (engine thread joins, audio
   stops cleanly).

## Smoke test without Flutter (optional)

Build `lazerdeck_engine`, then from any C/Dart harness:
`lazerdeck_init(2)` → `lazerdeck_load_file(0, "<some>.wav")` → `lazerdeck_play(0)`
→ poll `lazerdeck_get_deck_state(0,&s)` and watch `current_frame`/`is_playing`
advance → `lazerdeck_pause(0)`.

## Out of scope (PoC)

- Waveform / beatgrid / FFT visualization in Flutter.
- Actual VST3 effect processing (only the load boundary is wired).
- LazerScript editor UI (commands still reachable via `push_command`).
- macOS.

## Definition of done

Milestones 1–4 pass and the app plays/pauses a loaded file with a live timecode.
Engine builds 1–2 are verified in this pass; 3–4 are reproduced with the commands
in [07-build-and-tooling.md](07-build-and-tooling.md).
