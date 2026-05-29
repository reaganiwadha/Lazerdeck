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
  @ffi.Uint64()
  external int recallStart;
  @ffi.Uint64()
  external int recallEnd;
  @ffi.Int32()
  external int syncActive;
  @ffi.Int32()
  external int syncSource;
  @ffi.Int32()
  external int sampleRate;
  @ffi.Int32()
  external int metronomeEnabled;
  @ffi.Array(512)
  external ffi.Array<ffi.Char> filepath;
}

/// Mirror of C `LazerAudioDevice`. Field order/types must match lazerdeck.h.
final class LazerAudioDevice extends ffi.Struct {
  @ffi.Int32()
  external int index;
  @ffi.Int32()
  external int isDefault;
  @ffi.Int32()
  external int isCurrent;
  @ffi.Int32()
  external int maxOutputChannels;
  @ffi.Int32()
  external int defaultSampleRate;
  @ffi.Array(256)
  external ffi.Array<ffi.Char> name;
  @ffi.Array(64)
  external ffi.Array<ffi.Char> hostApi;
}

/// Mirror of C `LazerAudioConfig`.
final class LazerAudioConfig extends ffi.Struct {
  @ffi.Int32()
  external int deviceIndex;
  @ffi.Int32()
  external int sampleRate;
  @ffi.Int32()
  external int bufferFrames;
  @ffi.Int32()
  external int bitDepth;
  @ffi.Int32()
  external int latencyMs;
  @ffi.Array(256)
  external ffi.Array<ffi.Char> deviceName;
  @ffi.Array(64)
  external ffi.Array<ffi.Char> hostApi;
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

typedef _WaveInfoC = ffi.Int32 Function(
    ffi.Int32, ffi.Pointer<ffi.Uint64>, ffi.Pointer<ffi.Uint32>);
typedef _WaveInfoDart = int Function(
    int, ffi.Pointer<ffi.Uint64>, ffi.Pointer<ffi.Uint32>);

typedef _CopyBinsC = ffi.Int32 Function(ffi.Int32, ffi.Uint64, ffi.Uint32,
    ffi.Pointer<ffi.Float>, ffi.Pointer<ffi.Uint32>);
typedef _CopyBinsDart = int Function(
    int, int, int, ffi.Pointer<ffi.Float>, ffi.Pointer<ffi.Uint32>);

typedef _AudioDevCountC = ffi.Int32 Function();
typedef _AudioDevCountDart = int Function();

typedef _AudioDevC = ffi.Int32 Function(
    ffi.Int32, ffi.Pointer<LazerAudioDevice>);
typedef _AudioDevDart = int Function(int, ffi.Pointer<LazerAudioDevice>);

typedef _AudioCfgC = ffi.Int32 Function(ffi.Pointer<LazerAudioConfig>);
typedef _AudioCfgDart = int Function(ffi.Pointer<LazerAudioConfig>);

typedef _SetDevC = ffi.Int32 Function(ffi.Int32);
typedef _SetDevDart = int Function(int);

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
            'lazerdeck_push_command'),
        getWaveInfo = lib.lookupFunction<_WaveInfoC, _WaveInfoDart>(
            'lazerdeck_get_wave_info'),
        copyWaveBins = lib.lookupFunction<_CopyBinsC, _CopyBinsDart>(
            'lazerdeck_copy_wave_bins'),
        getAudioDeviceCount = lib.lookupFunction<_AudioDevCountC,
            _AudioDevCountDart>('lazerdeck_get_audio_device_count'),
        getAudioDevice = lib.lookupFunction<_AudioDevC, _AudioDevDart>(
            'lazerdeck_get_audio_device'),
        getAudioConfig = lib.lookupFunction<_AudioCfgC, _AudioCfgDart>(
            'lazerdeck_get_audio_config'),
        setAudioDevice = lib.lookupFunction<_SetDevC, _SetDevDart>(
            'lazerdeck_set_audio_device');

  final _InitDart init;
  final _VoidDart shutdown;
  final _IntDart getDeckCount;
  final _IntDart getSampleRate;
  final _DeckStateDart getDeckState;
  final _LoadDart loadFile;
  final _DeckCmdDart play;
  final _DeckCmdDart pause;
  final _PushCmdDart pushCommand;
  final _WaveInfoDart getWaveInfo;
  final _CopyBinsDart copyWaveBins;
  final _AudioDevCountDart getAudioDeviceCount;
  final _AudioDevDart getAudioDevice;
  final _AudioCfgDart getAudioConfig;
  final _SetDevDart setAudioDevice;

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
