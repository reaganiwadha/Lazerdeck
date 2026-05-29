# FFI contract

The C ABI between Flutter and the engine. Source of truth:
`engine/include/lazerdeck/lazerdeck.h`. Dart side:
`app/lib/ffi/lazerdeck_bindings.dart` (+ `engine.dart` wrapper).

## Functions

| C signature | Returns | Notes |
|-------------|---------|-------|
| `int32 lazerdeck_init(int32 num_decks)` | 1 ok / 0 fail | Spawns the engine thread; blocks until init done |
| `void  lazerdeck_shutdown()` | — | Stops the loop, joins the thread, frees the engine |
| `int32 lazerdeck_get_deck_count()` | count | |
| `int32 lazerdeck_get_sample_rate()` | Hz | `current_frame / sample_rate = seconds` |
| `int32 lazerdeck_get_deck_state(int32 deck, LazerDeckState* out)` | 1/0 | Fills `out` (caller-allocated) |
| `int32 lazerdeck_load_file(int32 deck, const char* path)` | 1/0 | Async load; routes via the command queue |
| `int32 lazerdeck_play(int32 deck)` | 1/0 | Calls `Deck::play()` directly |
| `int32 lazerdeck_pause(int32 deck)` | 1/0 | |
| `void  lazerdeck_push_command(const char* cmd)` | — | Raw LazerScript, e.g. `"$d1 seek 30"` |

## `LazerDeckState`

```c
typedef struct {
    float    bpm;
    float    beat_offset;
    float    speed;
    int32_t  is_playing;
    int32_t  is_loading;
    int32_t  is_analyzing;
    int32_t  loop_active;
    uint64_t current_frame;
    uint64_t loop_start;
    uint64_t loop_end;
    int32_t  sync_active;
    int32_t  sync_source;
    int32_t  sample_rate;   // added for timecode
    char     filepath[512];
} LazerDeckState;
```

The Dart `LazerDeckState extends ffi.Struct` mirror **must** keep this exact
field order/type (C ABI layout). If you change the C struct, update
`lazerdeck_bindings.dart` and any ffigen output together.

## Conventions

- Deck indices are **0-based** at the FFI (deck 0, 1, …). LazerScript commands
  are **1-based** (`$d1` == deck 0); `lazerdeck_load_file` does the conversion.
- All calls are safe to make from the Flutter UI isolate; they either push to a
  thread-safe queue or read atomics. None block on audio.
- Strings are UTF-8 `const char*`. The Dart wrapper owns allocation/free.

## Regenerating bindings (optional)

Bindings are hand-written (tiny surface, no LLVM needed). To regenerate with
ffigen instead, see `app/ffigen.yaml` and run `dart run ffigen` from `app/`
(requires LLVM/clang). Keep `engine.dart` as the stable wrapper either way.

## Future (waveform/visualization)

The PoC has no spectral/waveform UI. To add it, expose new FFI accessors:

- `int32 lazerdeck_get_waveform(int32 deck, float* out, int32 max_points, ...)`
  — downsampled peaks for drawing.
- `int32 lazerdeck_get_energy(int32 deck, uint64 frame, float* lowMidHigh)`
  — from `Deck::getFFTEnergy()` (`SimpleFFT`).

Return copies into caller buffers; never hand out engine-owned pointers.
