// Drives the native window title with live FPS + debug info. The title text is
// sent over the "lazerdeck/window" method channel, which the Windows runner
// turns into SetWindowText. No-op on platforms without that channel.
import 'dart:async';
import 'dart:io' show Platform;

import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';

import '../ffi/engine.dart';

class WindowTitleMonitor {
  WindowTitleMonitor(this.engine);

  final LazerdeckEngine engine;
  static const _channel = MethodChannel('lazerdeck/window');

  Timer? _timer;
  int _frames = 0;
  int _buildUs = 0;
  int _rasterUs = 0;
  int _worstRasterUs = 0;
  final Stopwatch _sw = Stopwatch();

  void start() {
    if (!Platform.isWindows) return; // only the Windows runner has the channel
    _sw.start();
    SchedulerBinding.instance.addTimingsCallback(_onTimings);
    _timer = Timer.periodic(const Duration(milliseconds: 500), (_) => _push());
  }

  void stop() {
    _timer?.cancel();
    _timer = null;
    if (Platform.isWindows) {
      SchedulerBinding.instance.removeTimingsCallback(_onTimings);
    }
  }

  void _onTimings(List<FrameTiming> timings) {
    for (final t in timings) {
      _frames++;
      _buildUs += t.buildDuration.inMicroseconds;
      _rasterUs += t.rasterDuration.inMicroseconds;
      final total = t.totalSpan.inMicroseconds;
      if (total > _worstRasterUs) _worstRasterUs = total;
    }
  }

  void _push() {
    final secs = _sw.elapsedMicroseconds / 1e6;
    final fps = (secs > 0) ? _frames / secs : 0.0;
    final build = _frames > 0 ? _buildUs / _frames / 1000.0 : 0.0;
    final raster = _frames > 0 ? _rasterUs / _frames / 1000.0 : 0.0;
    final worst = _worstRasterUs / 1000.0;

    final perf =
        '${fps.toStringAsFixed(0)} FPS  build ${build.toStringAsFixed(1)}ms  '
        'raster ${raster.toStringAsFixed(1)}ms  worst ${worst.toStringAsFixed(1)}ms';

    final cfg = engine.audioConfig();
    final audio = cfg == null
        ? 'audio: --'
        : '${cfg.sampleRate ~/ 1000}.${(cfg.sampleRate % 1000) ~/ 100}kHz '
            '${cfg.bufferFrames}f ~${cfg.latencyMs}ms';

    final decks = StringBuffer();
    for (var i = 0; i < engine.deckCount; i++) {
      final s = engine.deckState(i);
      final letter = String.fromCharCode(0x41 + i);
      final glyph = (s?.isPlaying ?? false) ? '▶' : '■';
      final bpm = (s != null && s.bpm > 0)
          ? (s.bpm * s.speed).toStringAsFixed(1)
          : '--';
      if (i > 0) decks.write(' ');
      decks.write('$letter$glyph$bpm');
    }

    final title = 'Lazerdeck  |  $perf  |  $audio  |  $decks';
    _channel.invokeMethod<void>('setTitle', title).catchError((_) {});

    // Reset the rolling window.
    _frames = 0;
    _buildUs = 0;
    _rasterUs = 0;
    _worstRasterUs = 0;
    _sw
      ..reset()
      ..start();
  }
}
