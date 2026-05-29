# Architecture

```
┌──────────────────────────────────────────────────────────┐
│ Flutter app (app/)                                         │
│   lib/ui/decks_view.dart   ← polls deck state ~25 Hz       │
│   lib/ffi/engine.dart      ← high-level Dart wrapper       │
│   lib/ffi/lazerdeck_bindings.dart  ← dart:ffi lookups      │
└───────────────┬────────────────────────────────────────────┘
                │  C ABI (engine/include/lazerdeck/lazerdeck.h)
                ▼
┌──────────────────────────────────────────────────────────┐
│ lazerdeck_engine (engine/) — SHARED lib, headless          │
│                                                            │
│  lazerdeck_ffi.cpp  → owns Engine on its own thread        │
│        │                                                   │
│   ┌────▼─────┐  pushCommand()/typed calls                  │
│   │  Engine  │  run(): service loop (commands/tasks/sync)  │
│   └──┬───┬───┘                                             │
│      │   │ owns                                            │
│  ┌───▼┐ ┌▼──────────┐ ┌──────────────┐ ┌────────────────┐ │
│  │Deck│ │AudioEngine │ │ Mixer (chans)│ │ OSCHandler     │ │
│  │ x N│ │(PortAudio) │ │              │ │ (UDP :9000)    │ │
│  └────┘ └─────┬──────┘ └──────┬───────┘ └────────────────┘ │
│               │ callback      │ VST calls (if loaded)      │
│               ▼               ▼                            │
│         speakers       Vst3Runtime (dlopen) ─ ─ ─ ─ ┐      │
└─────────────────────────────────────────────────────│──────┘
                                                       ▼ optional
                                   ┌──────────────────────────────┐
                                   │ lazerdeck_vst3 (plugins/vst3) │
                                   │  the only VST3 SDK consumer    │
                                   └──────────────────────────────┘
```

## Threads

- **Flutter UI isolate** — calls FFI functions; polls `lazerdeck_get_deck_state`
  on a `Timer`. FFI calls are synchronous and cheap (atomic reads / queue push).
- **Engine service thread** — spawned by `lazerdeck_init`; runs `Engine::run()`,
  a ~5 ms loop that drains the command queue and runs `processTasks` /
  `checkTriggers` / `updateSync`. No rendering, no SDL.
- **PortAudio callback thread** — owned by `AudioEngine`; pulls audio from each
  `Deck` (RubberBand time-stretch), sums through `Mixer` channels (optional VST),
  writes to the device. Independent of the service loop.
- **Per-deck loader/analysis threads** — `Deck::load` spawns a loader (miniaudio
  decode) and a BPM-analysis thread (SoundTouch). Spectral analysis uses the
  vendored `SimpleFFT`.

## Data flow for the PoC

1. User clicks **Open** → Dart `FilePicker` → `lazerdeck_load_file(deck, path)`.
2. FFI enqueues `"$dN load <path>"`; the service loop calls `Deck::load`, which
   decodes on a background thread and starts BPM analysis.
3. User clicks **Play** → `lazerdeck_play(deck)` → `Deck::play()` (atomic flag).
4. PortAudio callback advances `Deck::currentFrame`; UI polls
   `lazerdeck_get_deck_state` and renders `current_frame / sample_rate` as a
   timecode.

## Key invariants

- The engine never includes SDL, Qt, or the VST3 SDK.
- Anything Steinberg stays behind the `LzrVst3Api` boundary in `lazerdeck_vst3`.
- The audio callback never blocks (no file IO, no allocation in steady state).
