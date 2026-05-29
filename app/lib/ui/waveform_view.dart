// Scrolling, playhead-centered waveform — the Flutter equivalent of the old
// SDL renderer's deck view. Peaks and FFT colors are precomputed by the engine
// (see WaveformData); this widget only transforms and batches them into a
// single GPU draw call per frame.
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

import '../ffi/engine.dart';

// spp zoom limits (source frames per screen pixel).
const double _kSppMin = 32.0;    // most zoomed in
const double _kSppMax = 8192.0;  // most zoomed out
const double _kSppDefault = 256.0;
// Each button press multiplies / divides by this factor (one "octave" step).
const double _kZoomStep = 2.0;

class WaveformView extends StatefulWidget {
  final LazerdeckEngine engine;
  final int deck;

  /// Initial horizontal zoom (source frames per pixel). Adjustable at runtime
  /// with the +/- buttons or scroll wheel.
  final double samplesPerPixel;

  const WaveformView({
    super.key,
    required this.engine,
    required this.deck,
    this.samplesPerPixel = _kSppDefault,
  });

  @override
  State<WaveformView> createState() => _WaveformViewState();
}

class _WaveformViewState extends State<WaveformView>
    with SingleTickerProviderStateMixin {
  late final Ticker _ticker;
  final _model = _WaveModel();
  final _wf = WaveformData();
  String _lastPath = '';

  // Target spp drives smooth zoom; the model interpolates toward it each tick.
  double _targetSpp = _kSppDefault;

  @override
  void initState() {
    super.initState();
    _targetSpp = widget.samplesPerPixel;
    _model.samplesPerPixel = widget.samplesPerPixel;
    _ticker = createTicker(_onTick)..start();
  }

  void _onTick(Duration _) {
    final s = widget.engine.deckState(widget.deck);
    if (s == null) return;

    if (s.filepath != _lastPath) {
      _lastPath = s.filepath;
      _wf.invalidate();
    }
    widget.engine.updateWaveform(widget.deck, _wf);

    // Smooth zoom: exponential ease toward the target in log-space so each
    // step feels proportional regardless of current zoom level.
    final current = _model.samplesPerPixel;
    final diff = _targetSpp - current;
    _model.samplesPerPixel =
        (diff.abs() < 0.5) ? _targetSpp : current + diff * 0.18;

    _model
      ..wf = _wf
      ..currentFrame = s.currentFrame.toDouble()
      ..speed = s.speed
      ..bpm = s.bpm
      ..beatOffset = s.beatOffset
      ..sampleRate = s.sampleRate
      ..loading = s.isLoading
      ..tick();
  }

  void _zoomIn() => _setZoom(_targetSpp / _kZoomStep);
  void _zoomOut() => _setZoom(_targetSpp * _kZoomStep);

  void _setZoom(double spp) {
    _targetSpp = spp.clamp(_kSppMin, _kSppMax);
  }

  void _onScroll(PointerScrollEvent e) {
    // Scroll up = zoom in (smaller spp), scroll down = zoom out.
    if (e.scrollDelta.dy < 0) {
      _setZoom(_targetSpp / math.pow(_kZoomStep, 0.25));
    } else if (e.scrollDelta.dy > 0) {
      _setZoom(_targetSpp * math.pow(_kZoomStep, 0.25));
    }
  }

  @override
  void dispose() {
    _ticker.dispose();
    _model.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(8),
      child: Listener(
        onPointerSignal: (e) {
          if (e is PointerScrollEvent) _onScroll(e);
        },
        child: Stack(
          fit: StackFit.expand,
          children: [
            CustomPaint(painter: _WavePainter(_model), size: Size.infinite),
            Positioned(
              right: 6,
              bottom: 6,
              child: _ZoomButtons(onZoomIn: _zoomIn, onZoomOut: _zoomOut),
            ),
          ],
        ),
      ),
    );
  }
}

class _ZoomButtons extends StatelessWidget {
  final VoidCallback onZoomIn;
  final VoidCallback onZoomOut;
  const _ZoomButtons({required this.onZoomIn, required this.onZoomOut});

  @override
  Widget build(BuildContext context) {
    return DecoratedBox(
      decoration: BoxDecoration(
        color: Colors.black54,
        borderRadius: BorderRadius.circular(6),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          _ZoomBtn(icon: Icons.remove, tooltip: 'Zoom out', onTap: onZoomOut),
          Container(width: 1, height: 18, color: Colors.white12),
          _ZoomBtn(icon: Icons.add, tooltip: 'Zoom in', onTap: onZoomIn),
        ],
      ),
    );
  }
}

class _ZoomBtn extends StatelessWidget {
  final IconData icon;
  final String tooltip;
  final VoidCallback onTap;
  const _ZoomBtn({required this.icon, required this.tooltip, required this.onTap});

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: tooltip,
      child: InkWell(
        borderRadius: BorderRadius.circular(6),
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 5),
          child: Icon(icon, size: 14, color: Colors.white70),
        ),
      ),
    );
  }
}

/// Mutable per-frame state shared between the ticker and the painter. Updating
/// fields then calling [tick] repaints the CustomPaint's layer without
/// rebuilding the widget tree.
class _WaveModel extends ChangeNotifier {
  WaveformData wf = WaveformData();
  double currentFrame = 0;
  double speed = 1;
  double bpm = 0;
  double beatOffset = 0;
  int sampleRate = 44100;
  double samplesPerPixel = 256;
  bool loading = false;

  void tick() => notifyListeners();
}

class _WavePainter extends CustomPainter {
  final _WaveModel m;
  _WavePainter(this.m) : super(repaint: m);

  static const _bg = Color(0xFF0A0A0A);
  static const _gridColor = Color(0x33FFFFFF);
  static const _playheadColor = Color(0xFFFF3366);

  @override
  void paint(Canvas canvas, Size size) {
    final w = size.width;
    final h = size.height;
    canvas.drawRect(Offset.zero & size, Paint()..color = _bg);

    final wf = m.wf;
    if (wf.isEmpty) {
      _drawCentered(canvas, size, m.loading ? 'Preparing waveform…' : '');
      return;
    }

    final half = w / 2;
    final cy = h / 2;
    final scale = h / 2 * 0.9;

    // Visual samples-per-pixel: speed up the scroll with tempo so a sped-up
    // track still covers the same screen width per beat (mirrors the engine's
    // effectiveSPP = samplesPerPixel / stretchFactor).
    final speed = m.speed < 0.01 ? 0.01 : m.speed;
    double spp = m.samplesPerPixel * speed;
    if (spp < 1) spp = 1;

    final binFrames = wf.binFrames.toDouble();
    final cols = w.ceil();

    // Two triangles (6 vertices, 12 floats) per pixel column.
    final positions = Float32List(cols * 12);
    final colors = Int32List(cols * 6);
    var vp = 0;
    var cp = 0;

    for (var x = 0; x < cols; x++) {
      final frameCenter = m.currentFrame + (x - half) * spp;
      if (frameCenter < 0) continue;

      final f0 = frameCenter;
      final f1 = frameCenter + spp;
      var b0 = (f0 / binFrames).floor();
      var b1 = (f1 / binFrames).ceil();
      if (b0 < 0) b0 = 0;
      if (b0 >= wf.binCount) break;
      if (b1 > wf.binCount) b1 = wf.binCount;
      if (b1 <= b0) b1 = b0 + 1;

      // Accumulate peak/min across spanned bins; max-preserve transient per the
      // guide ("averaging transients makes drums disappear visually").
      var mn = 1.0;
      var mx = -1.0;
      var rmsSumSq = 0.0;
      var maxTransient = 0.0;
      for (var b = b0; b < b1; b++) {
        final lo = wf.data[b * 4];
        final hi = wf.data[b * 4 + 1];
        final r  = wf.data[b * 4 + 2];
        final t  = wf.data[b * 4 + 3];
        if (lo < mn) mn = lo;
        if (hi > mx) mx = hi;
        rmsSumSq += r * r;
        if (t > maxTransient) maxTransient = t;
      }
      if (mn > mx) { mn = 0; mx = 0; }
      final rms = math.sqrt(rmsSumSq / (b1 - b0));
      final color = wf.colors[b0];

      // Punchy visual height: RMS-compressed body + transient-boosted peaks.
      //   body      = dynamic-range-compressed RMS floor
      //   peak      = raw excursion, boosted at transient sites
      //   direction = preserved (waveform is asymmetric, not a mirrored bar)
      final body          = math.pow(rms, 0.55);
      final transientBoost = 1.0 + 1.8 * maxTransient;
      final displayMx     = math.max(body,  mx * transientBoost);
      final displayMn     = math.min(-body, mn * transientBoost);

      // Screen Y grows downward; positive sample → above center.
      var yTop = cy - displayMx * scale;
      var yBot = cy - displayMn * scale;
      if (yBot - yTop < 1) yBot = yTop + 1; // keep silence as a hairline

      final xL = x.toDouble();
      final xR = x + 1.0;

      // tri 1
      positions[vp++] = xL; positions[vp++] = yTop;
      positions[vp++] = xR; positions[vp++] = yTop;
      positions[vp++] = xL; positions[vp++] = yBot;
      // tri 2
      positions[vp++] = xR; positions[vp++] = yTop;
      positions[vp++] = xR; positions[vp++] = yBot;
      positions[vp++] = xL; positions[vp++] = yBot;

      for (var k = 0; k < 6; k++) {
        colors[cp++] = color;
      }
    }

    if (cp > 0) {
      final verts = ui.Vertices.raw(
        ui.VertexMode.triangles,
        Float32List.sublistView(positions, 0, vp),
        colors: Int32List.sublistView(colors, 0, cp),
      );
      canvas.drawVertices(verts, ui.BlendMode.srcOver, Paint());
    }

    _drawBeatGrid(canvas, size, spp, half);
    _drawPlayhead(canvas, size, half);
  }

  void _drawBeatGrid(Canvas canvas, Size size, double spp, double half) {
    if (m.bpm <= 0 || m.sampleRate <= 0) return;
    final framesPerBeat = m.sampleRate * 60.0 / m.bpm;
    final startFrame = m.currentFrame - half * spp;
    final endFrame = m.currentFrame + (size.width - half) * spp;
    final startBeat = ((startFrame - m.beatOffset) / framesPerBeat).floor();

    final line = Paint()
      ..color = _gridColor
      ..strokeWidth = 1;
    final downbeat = Paint()
      ..color = _gridColor.withValues(alpha: 0.5)
      ..strokeWidth = 2;

    for (var i = startBeat;; i++) {
      final beatFrame = i * framesPerBeat + m.beatOffset;
      if (beatFrame > endFrame) break;
      final x = half + (beatFrame - m.currentFrame) / spp;
      if (x < -2 || x > size.width + 2) continue;
      canvas.drawLine(
        Offset(x, 0),
        Offset(x, size.height),
        i % 4 == 0 ? downbeat : line,
      );
    }
  }

  void _drawPlayhead(Canvas canvas, Size size, double half) {
    canvas.drawLine(
      Offset(half, 0),
      Offset(half, size.height),
      Paint()
        ..color = _playheadColor
        ..strokeWidth = 2,
    );
  }

  void _drawCentered(Canvas canvas, Size size, String text) {
    if (text.isEmpty) return;
    final tp = TextPainter(
      text: TextSpan(
        text: text,
        style: const TextStyle(color: Colors.white38, fontSize: 13),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(
      canvas,
      Offset((size.width - tp.width) / 2, (size.height - tp.height) / 2),
    );
  }

  // Repaint is driven by the model Listenable; the widget itself never rebuilds
  // the painter, so there's nothing to diff here.
  @override
  bool shouldRepaint(_WavePainter oldDelegate) => false;
}
