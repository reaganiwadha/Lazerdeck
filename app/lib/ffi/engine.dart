// High-level Dart wrapper over the Lazerdeck engine FFI.
import 'dart:convert';
import 'dart:ffi' as ffi;

import 'package:ffi/ffi.dart';

import 'lazerdeck_bindings.dart';

/// Immutable snapshot of one deck's state for the UI.
class DeckState {
  final int index;
  final bool isPlaying;
  final bool isLoading;
  final bool isAnalyzing;
  final double bpm;
  final double speed;
  final int currentFrame;
  final int sampleRate;
  final String filepath;

  const DeckState({
    required this.index,
    required this.isPlaying,
    required this.isLoading,
    required this.isAnalyzing,
    required this.bpm,
    required this.speed,
    required this.currentFrame,
    required this.sampleRate,
    required this.filepath,
  });

  Duration get position => sampleRate > 0
      ? Duration(milliseconds: (currentFrame * 1000) ~/ sampleRate)
      : Duration.zero;

  bool get hasTrack => filepath.isNotEmpty;
}

/// Owns the native engine instance for the app's lifetime.
class LazerdeckEngine {
  final LazerdeckBindings _b;
  final ffi.Pointer<LazerDeckState> _scratch =
      calloc<LazerDeckState>(); // reused for polling
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
      speed: s.speed,
      currentFrame: s.currentFrame,
      sampleRate: s.sampleRate,
      filepath: _readFilepath(s),
    );
  }

  String _readFilepath(LazerDeckState s) {
    final bytes = <int>[];
    for (var i = 0; i < 512; i++) {
      final c = s.filepath[i] & 0xff;
      if (c == 0) break;
      bytes.add(c);
    }
    return utf8.decode(bytes, allowMalformed: true);
  }

  bool _disposed = false;

  void dispose() {
    if (_disposed) return;
    shutdown();
    calloc.free(_scratch);
    _disposed = true;
  }
}
