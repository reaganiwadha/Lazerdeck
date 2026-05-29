# Engine headless refactor

What changed to turn the SDL/Qt-coupled engine into a headless library.

## Removed from the engine

- `Renderer.{cpp,hpp}` — SDL2 rendering. Moved to `legacy/qt-sdl-ui/`.
- `ScriptEditor.*`, `LazerHighlighter.*`, `main.cpp` — Qt UI. Moved to `legacy/`.
- `#include <SDL2/SDL.h>` and the `Renderer renderer;` member in `Engine`.
- The SDL event/render loop and the input handlers
  (`handleEvents`, `handleGlobalInput`, `handleDeckInput`, `render`).
- Compiled-in VST3 SDK (now in `plugins/vst3/`, see
  [05-vst3-dynamic-plugin.md](05-vst3-dynamic-plugin.md)).
- The Windows `CoInitializeEx` calls (COM init now happens in the VST3 plugin).

## Added / changed

- **`Engine::run()`** is now a headless service loop:
  ```cpp
  while (running.load()) {
      processCommands();   // drains the LazerScript queue ($dN play/pause/load…)
      processTasks();      // tasks queued from OSC etc.
      checkTriggers();     // beat-synced triggers
      updateSync();        // deck tempo/phase sync
      std::this_thread::sleep_for(milliseconds(5));
  }
  ```
  Audio is unaffected — it runs on PortAudio's callback thread.
- **`processCommands()`** holds the `$dN …` command parser extracted from the
  old `run()` (no behavioural change).
- **`Clock.hpp`** provides `lzr::nowMs()` to replace `SDL_GetTicks64()` in
  `Deck::updateVisualFrame`.
- **`running`** is now `std::atomic<bool>` (stopped from the FFI thread).
- **`Engine::getSampleRate()`** / `getMixer()` accessors added for the FFI.
- **`SimpleFFT.hpp`** replaces FFTW (see [07](07-build-and-tooling.md) for why).
- **SoundTouch include** normalized to `<BPMDetect.h>` (FetchContent layout).

## Still present (intentionally)

- Full DSP: `Deck` (RubberBand stretch, SoundTouch BPM, spectral analysis),
  `AudioEngine` (PortAudio), `Mixer`.
- The OSC listener on UDP :9000 (`OSCHandler`) — kept as an external control path.
- LazerScript command handling in `Engine_OSC.cpp` / `processCommands`.

## Follow-ups / TODO

- Expose audio/FFT buffers over FFI so Flutter can draw waveforms & beatgrid
  (see [04-ffi-contract.md](04-ffi-contract.md) "Future").
- `updateVisualFrame` is currently unused in headless mode; wire it to a future
  FFI "smoothed position" accessor if Flutter wants interpolated playheads.
- Reconsider whether OSC should stay once Flutter fully drives the engine.
