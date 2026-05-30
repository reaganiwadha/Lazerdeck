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
  final double eqLow;
  final double eqMid;
  final double eqHigh;
  final double volume;
  final bool loopActive;
  final int loopStart;
  final int loopEnd;
  final int recallStart;
  final int recallEnd;
  final bool syncActive;
  final int syncSource;
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
    required this.eqLow,
    required this.eqMid,
    required this.eqHigh,
    required this.volume,
    required this.loopActive,
    required this.loopStart,
    required this.loopEnd,
    required this.recallStart,
    required this.recallEnd,
    required this.syncActive,
    required this.syncSource,
    required this.filepath,
  });

  bool get hasCue => loopStart > 0;
  bool get hasRecall => recallStart > 0;

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

  double get _framesPerBeat => bpm > 0 ? sampleRate * 60.0 / bpm : 0;

  /// Absolute beat index from the grid origin (0-based, can be negative before
  /// the first beat). 0 when BPM is unknown.
  double get beatPosition {
    final fpb = _framesPerBeat;
    return fpb > 0 ? (currentFrame - beatOffset) / fpb : 0;
  }

  /// 1-based bar number (4/4 assumed).
  int get bar => (beatPosition.floor() / 4).floor() + 1;

  /// 1-based beat within the current bar (1..4).
  int get beatInBar => (beatPosition.floor() % 4 + 4) % 4 + 1;

  /// Bar.beat counter, Rekordbox-style; "—.—" when BPM is unknown.
  String get barBeat => hasBpm ? '$bar.$beatInBar' : '—.—';
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

/// One automation lane (a LazerScript `onBeat` keyframe envelope) on a deck,
/// for drawing on top of the waveform. [beats] are absolute beats from the grid
/// origin; [values] are raw engine units (0..1, 0.5 = unity for EQ).
class AutomationLane {
  /// 0 = Low, 1 = Mid, 2 = High, 3 = Vol.
  final int paramId;
  final Float64List beats;
  final Float32List values;

  const AutomationLane(this.paramId, this.beats, this.values);

  static const _names = ['Low', 'Mid', 'High', 'Vol'];

  /// Short, display-friendly parameter name ("Low", "Mid", "High", "Vol").
  String get name => paramId >= 0 && paramId < _names.length
      ? _names[paramId]
      : 'P$paramId';
}

/// Kind of timeline marker. Index order must match the engine's `MarkerKind`
/// (lazerdeck.h): 0 = generic, 1 = play, 2 = jump. Append new kinds at the end.
enum MarkerKind { generic, play, jump }

/// One discrete beat event on a deck's timeline (a LazerScript trigger), for
/// drawing on the waveform. [beat] is the absolute (0-based, grid-origin) beat
/// on this deck where it fires; [targetDeck]/[targetBeat] describe what it does
/// to another deck (−1 / negative when not applicable). [label] is an
/// engine-formatted short display string (e.g. "▶ D2", "D2→16").
class DeckMarker {
  final MarkerKind kind;
  final double beat;
  final int targetDeck;
  final double targetBeat;
  final String label;

  const DeckMarker({
    required this.kind,
    required this.beat,
    required this.targetDeck,
    required this.targetBeat,
    required this.label,
  });
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

/// Snapshot of the HTTP/JSON control server's state. [running] is whether it is
/// currently serving; [port] is the last requested/bound port (127.0.0.1).
class ControlServerStatus {
  final bool running;
  final int port;
  const ControlServerStatus({required this.running, required this.port});
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
  // Reused scratch for the automation-lane poll. A deck has at most one lane
  // per param (low/mid/high/vol); _kMaxKeys bounds total keyframes per deck.
  static const int _kMaxLanes = 4;
  static const int _kMaxKeys = 512;
  final ffi.Pointer<ffi.Int32> _laneParamIds = calloc<ffi.Int32>(_kMaxLanes);
  final ffi.Pointer<ffi.Uint32> _laneKeyCounts = calloc<ffi.Uint32>(_kMaxLanes);
  final ffi.Pointer<ffi.Double> _laneBeats = calloc<ffi.Double>(_kMaxKeys);
  final ffi.Pointer<ffi.Float> _laneValues = calloc<ffi.Float>(_kMaxKeys);
  // Reused scratch for the marker poll. Mirrors LAZERDECK_MARKER_LABEL_LEN.
  static const int _kMaxMarkers = 64;
  static const int _kMarkerLabelLen = 48;
  final ffi.Pointer<ffi.Int32> _markerKinds = calloc<ffi.Int32>(_kMaxMarkers);
  final ffi.Pointer<ffi.Double> _markerBeats = calloc<ffi.Double>(_kMaxMarkers);
  final ffi.Pointer<ffi.Int32> _markerTargetDecks =
      calloc<ffi.Int32>(_kMaxMarkers);
  final ffi.Pointer<ffi.Double> _markerTargetBeats =
      calloc<ffi.Double>(_kMaxMarkers);
  final ffi.Pointer<ffi.Char> _markerLabels =
      calloc<ffi.Char>(_kMaxMarkers * _kMarkerLabelLen);
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

  /// Fires a single metronome click, independent of the beat grid (used by the
  /// Tap Tempo wizard so each tap is audible).
  void metronomeTick(int deck) => pushCommand('${_d(deck)} metronome_tick');

  /// Deletes this track's cached analysis and re-runs BPM detection.
  void reanalyze(int deck) => pushCommand('${_d(deck)} reanalyze');

  /// Nudge tempo up/down by ~1 BPM (engine-clamped), or reset to 0% (1.0x).
  void speedUp(int deck) => pushCommand('${_d(deck)} speed_up');
  void speedDown(int deck) => pushCommand('${_d(deck)} speed_down');
  void resetSpeed(int deck) => pushCommand('${_d(deck)} speed_reset');
  void setSpeed(int deck, double speed) =>
      pushCommand('${_d(deck)} speed $speed');

  /// Channel volume (linear gain, 0..1).
  void setVolume(int deck, double value) =>
      pushCommand('${_d(deck)} volume $value');

  /// 3-band channel EQ. [value] is 0..1 with 0.5 = unity (0 dB), 1.0 = +6 dB,
  /// 0.0 = full kill — a DJM-style curve handled engine-side.
  void setEqLow(int deck, double value) =>
      pushCommand('${_d(deck)} eq_low $value');
  void setEqMid(int deck, double value) =>
      pushCommand('${_d(deck)} eq_mid $value');
  void setEqHigh(int deck, double value) =>
      pushCommand('${_d(deck)} eq_high $value');

  /// Loop in-point (A), out-point (B, activates the loop), and exit.
  void loopIn(int deck) => pushCommand('${_d(deck)} loop_in');
  void loopOut(int deck) => pushCommand('${_d(deck)} loop_out');
  void exitLoop(int deck) => pushCommand('${_d(deck)} loop_exit');
  void clearLoop(int deck) => pushCommand('${_d(deck)} loop_clear');
  void reloop(int deck) => pushCommand('${_d(deck)} reloop');

  /// Toggles this deck's beat-sync intent. The engine elects the master deck and
  /// points followers at it, so [sourceDeck] only signals on (>= 0) vs off (-1);
  /// the actual source is always the current master.
  void setSync(int deck, int sourceDeck) {
    final arg = sourceDeck >= 0 ? sourceDeck + 1 : 0;
    pushCommand('${_d(deck)} sync $arg');
  }

  /// The deck index currently elected as the beat-sync master (auto-promotes to
  /// a playing deck; pausing/unloading the master hands it to what's playing).
  int masterDeck() => _initialized ? _b.getMasterDeck() : 0;

  /// Manually designate [deck] as the beat-sync master. Holds only while that
  /// deck keeps playing — the engine re-promotes to a playing deck otherwise.
  void setMaster(int deck) => pushCommand('${_d(deck)} master');

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
      eqLow: s.eqLow,
      eqMid: s.eqMid,
      eqHigh: s.eqHigh,
      volume: s.volume,
      loopActive: s.loopActive != 0,
      loopStart: s.loopStart,
      loopEnd: s.loopEnd,
      recallStart: s.recallStart,
      recallEnd: s.recallEnd,
      syncActive: s.syncActive != 0,
      syncSource: s.syncSource,
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

  /// Snapshots [deck]'s automation lanes (LazerScript `onBeat` envelopes) for
  /// the waveform overlay. Cheap enough to poll each frame; returns an empty
  /// list when the deck has no automation. Keyframes beyond [_kMaxKeys] total
  /// per deck are dropped (far more than any usable script).
  List<AutomationLane> deckLanes(int deck) {
    if (!_initialized) return const [];
    final n = _b.getLanes(deck, _kMaxLanes, _kMaxKeys, _laneParamIds,
        _laneKeyCounts, _laneBeats, _laneValues);
    if (n <= 0) return const [];

    final lanes = <AutomationLane>[];
    var off = 0;
    for (var i = 0; i < n; i++) {
      final count = _laneKeyCounts[i];
      final beats = Float64List(count);
      final values = Float32List(count);
      for (var k = 0; k < count; k++) {
        beats[k] = _laneBeats[off + k];
        values[k] = _laneValues[off + k];
      }
      off += count;
      lanes.add(AutomationLane(_laneParamIds[i], beats, values));
    }
    return lanes;
  }

  /// Snapshots [deck]'s timeline markers (LazerScript triggers like
  /// `play when …`). Cheap to poll each frame; returns an empty list when the
  /// deck has none. Markers beyond [_kMaxMarkers] are dropped.
  List<DeckMarker> deckMarkers(int deck) {
    if (!_initialized) return const [];
    final n = _b.getMarkers(deck, _kMaxMarkers, _markerKinds, _markerBeats,
        _markerTargetDecks, _markerTargetBeats, _markerLabels);
    if (n <= 0) return const [];

    final out = <DeckMarker>[];
    for (var i = 0; i < n; i++) {
      final k = _markerKinds[i];
      out.add(DeckMarker(
        kind: (k >= 0 && k < MarkerKind.values.length)
            ? MarkerKind.values[k]
            : MarkerKind.generic,
        beat: _markerBeats[i],
        targetDeck: _markerTargetDecks[i],
        targetBeat: _markerTargetBeats[i],
        label: _readMarkerLabel(i),
      ));
    }
    return out;
  }

  String _readMarkerLabel(int i) {
    final p = _markerLabels.cast<ffi.Uint8>();
    final base = i * _kMarkerLabelLen;
    final bytes = <int>[];
    for (var j = 0; j < _kMarkerLabelLen; j++) {
      final c = p[base + j];
      if (c == 0) break;
      bytes.add(c);
    }
    return utf8.decode(bytes, allowMalformed: true);
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

  /// Sample rates the settings UI offers. The engine falls back to the nearest
  /// rate the device actually supports, so these are requests, not guarantees.
  static const List<int> supportedSampleRates = [44100, 48000, 88200, 96000];

  /// Asynchronously sets the engine's output sample rate. The engine reopens the
  /// stream and re-decodes loaded tracks at the new rate on its own thread
  /// (position + play state preserved; loops/cues reset). Poll [audioConfig].
  void setSampleRate(int rate) {
    if (!_initialized) return;
    _b.setSampleRate(rate);
  }

  /// Default port the engine tries to bind the JSON control server to at startup.
  static const int defaultControlPort = 8203;

  /// Live state of the HTTP/JSON control server (see [controlServerStatus]).
  ControlServerStatus controlServerStatus() {
    if (!_initialized) {
      return const ControlServerStatus(running: false, port: defaultControlPort);
    }
    return ControlServerStatus(
      running: _b.controlIsRunning() == 1,
      port: _b.controlGetPort(),
    );
  }

  /// Binds the control server to [port] on 127.0.0.1 (restarting it if already
  /// running). Returns true iff the bind succeeded; false means the port is busy
  /// — try another. Non-fatal either way.
  bool startControlServer(int port) {
    if (!_initialized) return false;
    return _b.controlStart(port) == 1;
  }

  /// Stops the control server.
  void stopControlServer() {
    if (!_initialized) return;
    _b.controlStop();
  }

  bool _disposed = false;

  void dispose() {
    if (_disposed) return;
    shutdown();
    calloc.free(_scratch);
    calloc.free(_binCountOut);
    calloc.free(_binFramesOut);
    calloc.free(_laneParamIds);
    calloc.free(_laneKeyCounts);
    calloc.free(_laneBeats);
    calloc.free(_laneValues);
    calloc.free(_markerKinds);
    calloc.free(_markerBeats);
    calloc.free(_markerTargetDecks);
    calloc.free(_markerTargetBeats);
    calloc.free(_markerLabels);
    if (_binMinMax != ffi.nullptr) calloc.free(_binMinMax);
    if (_binRgba != ffi.nullptr) calloc.free(_binRgba);
    _disposed = true;
  }
}
