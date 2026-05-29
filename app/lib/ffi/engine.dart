// High-level Dart wrapper over the Lazerdeck engine FFI.
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'lazerdeck_bindings.dart';

/// Immutable snapshot of one deck's state for the UI.
class DeckState {
  final int index;
  final bool isPlaying;
  final bool isLoading;
  final bool isAnalyzing;
  final double bpm;
  final double beatOffset;
  final double speed;
  final int currentFrame;
  final int sampleRate;
  final bool metronomeEnabled;
  final String filepath;

  const DeckState({
    required this.index,
    required this.isPlaying,
    required this.isLoading,
    required this.isAnalyzing,
    required this.bpm,
    required this.beatOffset,
    required this.speed,
    required this.currentFrame,
    required this.sampleRate,
    required this.metronomeEnabled,
    required this.filepath,
  });

  Duration get position => sampleRate > 0
      ? Duration(milliseconds: (currentFrame * 1000) ~/ sampleRate)
      : Duration.zero;

  bool get hasTrack => filepath.isNotEmpty;

  /// True once a usable BPM is known (from analysis, the DB, or manual entry).
  bool get hasBpm => bpm > 0;

  /// Tempo after the pitch/speed adjustment (what's actually heard).
  double get effectiveBpm => bpm * speed;

  /// Pitch adjustment as a signed percentage, Rekordbox-style (e.g. +2.5).
  double get speedPercent => (speed - 1.0) * 100.0;

  /// Beat-grid offset expressed in milliseconds.
  double get offsetMs => sampleRate > 0 ? beatOffset / sampleRate * 1000.0 : 0;
}

/// Host-side cache of one deck's precomputed waveform summary.
///
/// [data] holds 4 floats per bin: [mn, mx, rms, transient].
/// [colors] holds one 0xAARRGGBB value per bin.
/// Indexed directly by the painter — no FFI calls happen during paint.
class WaveformData {
  int binFrames = 0;
  int binCount = 0;
  Float32List data = Float32List(0);
  Int32List colors = Int32List(0);

  bool get isEmpty => binCount == 0;

  /// Forces the next [LazerdeckEngine.updateWaveform] to re-copy, e.g. after
  /// the deck loads a different track.
  void invalidate() {
    binCount = 0;
    binFrames = 0;
  }
}

/// A selectable audio output device.
class AudioDevice {
  final int index;
  final String name;
  final String hostApi;
  final int maxOutputChannels;
  final int defaultSampleRate;
  final bool isDefault;
  final bool isCurrent;

  const AudioDevice({
    required this.index,
    required this.name,
    required this.hostApi,
    required this.maxOutputChannels,
    required this.defaultSampleRate,
    required this.isDefault,
    required this.isCurrent,
  });
}

/// The engine's live audio output configuration.
class AudioConfig {
  final int deviceIndex;
  final int sampleRate;
  final int bufferFrames;
  final int bitDepth;
  final int latencyMs;
  final String deviceName;
  final String hostApi;

  const AudioConfig({
    required this.deviceIndex,
    required this.sampleRate,
    required this.bufferFrames,
    required this.bitDepth,
    required this.latencyMs,
    required this.deviceName,
    required this.hostApi,
  });
}

/// Owns the native engine instance for the app's lifetime.
class LazerdeckEngine {
  final LazerdeckBindings _b;
  final ffi.Pointer<LazerDeckState> _scratch =
      calloc<LazerDeckState>(); // reused for polling
  // Reused scratch for the cheap wave-info poll.
  final ffi.Pointer<ffi.Uint64> _binCountOut = calloc<ffi.Uint64>();
  final ffi.Pointer<ffi.Uint32> _binFramesOut = calloc<ffi.Uint32>();
  // Native staging buffers for bulk bin copies, grown on demand.
  ffi.Pointer<ffi.Float> _binMinMax = ffi.nullptr;
  ffi.Pointer<ffi.Uint32> _binRgba = ffi.nullptr;
  int _binCapacity = 0;
  int _deckCount = 0;
  bool _initialized = false;

  LazerdeckEngine._(this._b);

  static LazerdeckEngine load() => LazerdeckEngine._(LazerdeckBindings.open());

  int get deckCount => _deckCount;
  bool get isInitialized => _initialized;

  bool init({int numDecks = 2}) {
    if (_initialized) return true;
    final ok = _b.init(numDecks) == 1;
    if (ok) {
      _initialized = true;
      _deckCount = _b.getDeckCount();
    }
    return ok;
  }

  void shutdown() {
    if (!_initialized) return;
    _b.shutdown();
    _initialized = false;
    _deckCount = 0;
  }

  void loadFile(int deck, String path) {
    final p = path.toNativeUtf8();
    try {
      _b.loadFile(deck, p.cast());
    } finally {
      calloc.free(p);
    }
  }

  void play(int deck) => _b.play(deck);
  void pause(int deck) => _b.pause(deck);

  // Decks are 1-based in LazerScript ($d1, $d2, …).
  String _d(int deck) => '\$d${deck + 1}';

  /// Sets the deck's BPM by hand; pins it against auto-analysis and persists it.
  void setBpm(int deck, double bpm) => pushCommand('${_d(deck)} bpm $bpm');

  /// Sets the beat-grid offset, in source frames (absolute).
  void setBeatOffset(int deck, double frames) =>
      pushCommand('${_d(deck)} offset $frames');

  /// Nudges the beat-grid offset by [deltaFrames] (signed).
  void nudgeBeatOffset(int deck, double deltaFrames) =>
      pushCommand('${_d(deck)} nudge_offset $deltaFrames');

  /// Nudges the offset by [ms] milliseconds, using the deck's sample rate.
  void nudgeBeatOffsetMs(int deck, double ms) {
    final sr = deckState(deck)?.sampleRate ?? 0;
    if (sr <= 0) return;
    nudgeBeatOffset(deck, ms / 1000.0 * sr);
  }

  void setMetronome(int deck, bool on) =>
      pushCommand('${_d(deck)} metronome ${on ? 1 : 0}');

  void pushCommand(String cmd) {
    final p = cmd.toNativeUtf8();
    try {
      _b.pushCommand(p.cast());
    } finally {
      calloc.free(p);
    }
  }

  /// Polls the current state of [deck]; returns null if unavailable.
  DeckState? deckState(int deck) {
    if (!_initialized) return null;
    if (_b.getDeckState(deck, _scratch) != 1) return null;
    final s = _scratch.ref;
    return DeckState(
      index: deck,
      isPlaying: s.isPlaying != 0,
      isLoading: s.isLoading != 0,
      isAnalyzing: s.isAnalyzing != 0,
      bpm: s.bpm,
      beatOffset: s.beatOffset,
      speed: s.speed,
      currentFrame: s.currentFrame,
      sampleRate: s.sampleRate,
      metronomeEnabled: s.metronomeEnabled != 0,
      filepath: _readFilepath(s),
    );
  }

  /// Refreshes [out] for [deck] if the engine's bin count changed (it grows as
  /// a track streams in). A no-op when nothing changed, so it's cheap to call
  /// every frame. Call [WaveformData.invalidate] on track change to force a
  /// re-copy even if the new bin count happens to match the old one.
  void updateWaveform(int deck, WaveformData out) {
    if (!_initialized) return;
    if (_b.getWaveInfo(deck, _binCountOut, _binFramesOut) != 1) return;

    final count = _binCountOut.value;
    final frames = _binFramesOut.value;
    if (count == out.binCount && frames == out.binFrames) return;

    if (count == 0) {
      out
        ..binCount = 0
        ..binFrames = frames
        ..data = Float32List(0)
        ..colors = Int32List(0);
      return;
    }

    if (count > _binCapacity) {
      if (_binMinMax != ffi.nullptr) calloc.free(_binMinMax);
      if (_binRgba != ffi.nullptr) calloc.free(_binRgba);
      _binCapacity = count;
      _binMinMax = calloc<ffi.Float>(count * 4); // 4 floats per bin
      _binRgba = calloc<ffi.Uint32>(count);
    }

    final copied = _b.copyWaveBins(deck, 0, count, _binMinMax, _binRgba);
    out.binFrames = frames;
    out.binCount = copied;
    // Copy out of native scratch into Dart-owned lists so reuse of the staging
    // buffers can't corrupt what the painter is reading.
    out.data = Float32List.fromList(_binMinMax.asTypedList(copied * 4));
    out.colors =
        Int32List.fromList(_binRgba.cast<ffi.Int32>().asTypedList(copied));
  }

  String _readFilepath(LazerDeckState s) => _readChars(s.filepath, 512);

  String _readChars(ffi.Array<ffi.Char> arr, int max) {
    final bytes = <int>[];
    for (var i = 0; i < max; i++) {
      final c = arr[i] & 0xff;
      if (c == 0) break;
      bytes.add(c);
    }
    return utf8.decode(bytes, allowMalformed: true);
  }

  /// Enumerates the available audio output devices. Cheap enough to call when
  /// opening the settings panel; not meant for per-frame use.
  List<AudioDevice> audioDevices() {
    if (!_initialized) return const [];
    final count = _b.getAudioDeviceCount();
    if (count <= 0) return const [];
    final p = calloc<LazerAudioDevice>();
    try {
      final out = <AudioDevice>[];
      for (var i = 0; i < count; i++) {
        if (_b.getAudioDevice(i, p) != 1) continue;
        final d = p.ref;
        out.add(AudioDevice(
          index: d.index,
          name: _readChars(d.name, 256),
          hostApi: _readChars(d.hostApi, 64),
          maxOutputChannels: d.maxOutputChannels,
          defaultSampleRate: d.defaultSampleRate,
          isDefault: d.isDefault != 0,
          isCurrent: d.isCurrent != 0,
        ));
      }
      return out;
    } finally {
      calloc.free(p);
    }
  }

  /// The engine's current audio output configuration, or null if unavailable.
  AudioConfig? audioConfig() {
    if (!_initialized) return null;
    final p = calloc<LazerAudioConfig>();
    try {
      if (_b.getAudioConfig(p) != 1) return null;
      final c = p.ref;
      return AudioConfig(
        deviceIndex: c.deviceIndex,
        sampleRate: c.sampleRate,
        bufferFrames: c.bufferFrames,
        bitDepth: c.bitDepth,
        latencyMs: c.latencyMs,
        deviceName: _readChars(c.deviceName, 256),
        hostApi: _readChars(c.hostApi, 64),
      );
    } finally {
      calloc.free(p);
    }
  }

  /// Asynchronously switches the audio output to [deviceIndex] (a PortAudio
  /// index from [audioDevices]). The engine safely reopens its output stream on
  /// its own thread; poll [audioConfig] to observe the change.
  void switchAudioDevice(int deviceIndex) {
    if (!_initialized) return;
    _b.setAudioDevice(deviceIndex);
  }

  bool _disposed = false;

  void dispose() {
    if (_disposed) return;
    shutdown();
    calloc.free(_scratch);
    calloc.free(_binCountOut);
    calloc.free(_binFramesOut);
    if (_binMinMax != ffi.nullptr) calloc.free(_binMinMax);
    if (_binRgba != ffi.nullptr) calloc.free(_binRgba);
    _disposed = true;
  }
}
