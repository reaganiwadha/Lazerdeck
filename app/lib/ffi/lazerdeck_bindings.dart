// Low-level dart:ffi bindings to engine/include/lazerdeck/lazerdeck.h.
//
// Hand-written (the surface is tiny) so the build needs no ffigen/LLVM. If you
// prefer generation, see app/ffigen.yaml and run `dart run ffigen`.
//
// ignore_for_file: library_private_types_in_public_api
import 'dart:ffi' as ffi;
import 'dart:io' show Platform;

/// Mirror of the C `LazerDeckState` struct. Field order/types must match
/// lazerdeck.h exactly (C ABI layout).
final class LazerDeckState extends ffi.Struct {
  @ffi.Float()
  external double bpm;
  @ffi.Float()
  external double beatOffset;
  @ffi.Float()
  external double speed;
  @ffi.Int32()
  external int isPlaying;
  @ffi.Int32()
  external int isLoading;
  @ffi.Int32()
  external int isAnalyzing;
  @ffi.Int32()
  external int loopActive;
  @ffi.Uint64()
  external int currentFrame;
  @ffi.Uint64()
  external int loopStart;
  @ffi.Uint64()
  external int loopEnd;
  @ffi.Int32()
  external int syncActive;
  @ffi.Int32()
  external int syncSource;
  @ffi.Int32()
  external int sampleRate;
  @ffi.Array(512)
  external ffi.Array<ffi.Char> filepath;
}

// --- C function typedefs ---
typedef _InitC = ffi.Int32 Function(ffi.Int32);
typedef _InitDart = int Function(int);

typedef _VoidC = ffi.Void Function();
typedef _VoidDart = void Function();

typedef _IntC = ffi.Int32 Function();
typedef _IntDart = int Function();

typedef _DeckStateC = ffi.Int32 Function(ffi.Int32, ffi.Pointer<LazerDeckState>);
typedef _DeckStateDart = int Function(int, ffi.Pointer<LazerDeckState>);

typedef _LoadC = ffi.Int32 Function(ffi.Int32, ffi.Pointer<ffi.Char>);
typedef _LoadDart = int Function(int, ffi.Pointer<ffi.Char>);

typedef _DeckCmdC = ffi.Int32 Function(ffi.Int32);
typedef _DeckCmdDart = int Function(int);

typedef _PushCmdC = ffi.Void Function(ffi.Pointer<ffi.Char>);
typedef _PushCmdDart = void Function(ffi.Pointer<ffi.Char>);

/// Resolves all symbols from the lazerdeck engine shared library.
class LazerdeckBindings {
  LazerdeckBindings(ffi.DynamicLibrary lib)
      : init = lib.lookupFunction<_InitC, _InitDart>('lazerdeck_init'),
        shutdown = lib.lookupFunction<_VoidC, _VoidDart>('lazerdeck_shutdown'),
        getDeckCount =
            lib.lookupFunction<_IntC, _IntDart>('lazerdeck_get_deck_count'),
        getSampleRate =
            lib.lookupFunction<_IntC, _IntDart>('lazerdeck_get_sample_rate'),
        getDeckState = lib.lookupFunction<_DeckStateC, _DeckStateDart>(
            'lazerdeck_get_deck_state'),
        loadFile =
            lib.lookupFunction<_LoadC, _LoadDart>('lazerdeck_load_file'),
        play = lib.lookupFunction<_DeckCmdC, _DeckCmdDart>('lazerdeck_play'),
        pause = lib.lookupFunction<_DeckCmdC, _DeckCmdDart>('lazerdeck_pause'),
        pushCommand = lib.lookupFunction<_PushCmdC, _PushCmdDart>(
            'lazerdeck_push_command');

  final _InitDart init;
  final _VoidDart shutdown;
  final _IntDart getDeckCount;
  final _IntDart getSampleRate;
  final _DeckStateDart getDeckState;
  final _LoadDart loadFile;
  final _DeckCmdDart play;
  final _DeckCmdDart pause;
  final _PushCmdDart pushCommand;

  /// Opens the engine library bundled next to the executable.
  static LazerdeckBindings open() {
    final name = Platform.isWindows
        ? 'lazerdeck_engine.dll'
        : Platform.isMacOS
            ? 'liblazerdeck_engine.dylib'
            : 'liblazerdeck_engine.so';
    return LazerdeckBindings(ffi.DynamicLibrary.open(name));
  }
}
