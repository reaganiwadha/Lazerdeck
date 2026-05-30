// Per-deck channel controls shown beside the waveform, at the same height.
// No boxes/cards/outlines — just the controls floating on the background:
//   - MixerBox: 3-band isolator EQ stacked vertically (HI / MID / LOW) plus a
//     channel volume fader. EQ + volume are bound to the engine.
//   - FxBox: three placeholder FX knobs (not yet wired to the engine).
import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../ffi/engine.dart';

const _kCenter = 0.5; // EQ unity / 0 dB
const _accent = Color(0xFFE0344B);

const _labelStyle = TextStyle(
  fontSize: 9,
  fontFamily: 'bitroad',
  letterSpacing: 1,
  fontWeight: FontWeight.w600,
  color: Colors.white38,
);

/// Three placeholder FX knobs (FX1/FX2/FX3). Not wired to the engine yet —
/// they just turn so the layout is in place for future per-channel effects.
class FxBox extends StatefulWidget {
  const FxBox({super.key});

  @override
  State<FxBox> createState() => _FxBoxState();
}

class _FxBoxState extends State<FxBox> {
  double _a = _kCenter;
  double _b = _kCenter;
  double _c = _kCenter;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 56,
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          _EqKnob(
            label: 'FX1',
            value: _a,
            onChanged: (v) => setState(() => _a = v),
          ),
          _EqKnob(
            label: 'FX2',
            value: _b,
            onChanged: (v) => setState(() => _b = v),
          ),
          _EqKnob(
            label: 'FX3',
            value: _c,
            onChanged: (v) => setState(() => _c = v),
          ),
        ],
      ),
    );
  }
}

/// Channel mixer: vertical HI/MID/LOW isolator EQ + volume fader.
///
/// The knobs/fader display the engine's *current* values (from the polled
/// [state]) so LazerScript commands and beat automation visibly move them.
/// While the user is actively dragging a control we hold a local value for
/// smoothness, then resync from the engine on release.
class MixerBox extends StatefulWidget {
  final LazerdeckEngine engine;
  final int deck;
  final DeckState? state;
  const MixerBox({
    super.key,
    required this.engine,
    required this.deck,
    required this.state,
  });

  @override
  State<MixerBox> createState() => _MixerBoxState();
}

class _MixerBoxState extends State<MixerBox> {
  double _hi = _kCenter;
  double _mid = _kCenter;
  double _low = _kCenter;
  double _vol = 1.0;
  bool _dragging = false;

  @override
  Widget build(BuildContext context) {
    // Follow the engine unless the user is mid-drag.
    final s = widget.state;
    if (!_dragging && s != null) {
      _hi = s.eqHigh;
      _mid = s.eqMid;
      _low = s.eqLow;
      _vol = s.volume;
    }

    return SizedBox(
      width: 64,
      child: Column(
        children: [
          _EqKnob(
            label: 'HI',
            value: _hi,
            onChanged: (v) {
              setState(() {
                _dragging = true;
                _hi = v;
              });
              widget.engine.setEqHigh(widget.deck, v);
            },
            onChangeEnd: () => setState(() => _dragging = false),
          ),
          _EqKnob(
            label: 'MID',
            value: _mid,
            onChanged: (v) {
              setState(() {
                _dragging = true;
                _mid = v;
              });
              widget.engine.setEqMid(widget.deck, v);
            },
            onChangeEnd: () => setState(() => _dragging = false),
          ),
          _EqKnob(
            label: 'LOW',
            value: _low,
            onChanged: (v) {
              setState(() {
                _dragging = true;
                _low = v;
              });
              widget.engine.setEqLow(widget.deck, v);
            },
            onChangeEnd: () => setState(() => _dragging = false),
          ),
          const SizedBox(height: 10),
          Expanded(
            child: _VolumeFader(
              value: _vol,
              onChanged: (v) {
                setState(() {
                  _dragging = true;
                  _vol = v;
                });
                widget.engine.setVolume(widget.deck, v);
              },
              onChangeEnd: () => setState(() => _dragging = false),
            ),
          ),
          const SizedBox(height: 4),
          const Text('VOL', style: _labelStyle),
        ],
      ),
    );
  }
}

/// A small rotary knob. Drag vertically to turn; double-tap to recenter.
class _EqKnob extends StatelessWidget {
  final String label;
  final double value; // 0..1
  final ValueChanged<double> onChanged;
  final VoidCallback? onChangeEnd;

  const _EqKnob({
    required this.label,
    required this.value,
    required this.onChanged,
    this.onChangeEnd,
  });

  // Pixels of vertical drag for the full 0..1 sweep.
  static const _dragRange = 160.0;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 3),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          GestureDetector(
            behavior: HitTestBehavior.opaque,
            onDoubleTap: () {
              onChanged(_kCenter);
              onChangeEnd?.call();
            },
            onVerticalDragUpdate: (d) {
              final next = (value - d.delta.dy / _dragRange).clamp(0.0, 1.0);
              if (next != value) onChanged(next);
            },
            onVerticalDragEnd: (_) => onChangeEnd?.call(),
            child: SizedBox(
              width: 36,
              height: 36,
              child: CustomPaint(painter: _KnobPainter(value)),
            ),
          ),
          const SizedBox(height: 2),
          Text(label, style: _labelStyle),
        ],
      ),
    );
  }
}

class _KnobPainter extends CustomPainter {
  final double value; // 0..1
  _KnobPainter(this.value);

  // Sweep from 7 o'clock to 5 o'clock (270°), leaving a gap at the bottom.
  static const _startAngle = math.pi * 0.75; // 135°
  static const _sweep = math.pi * 1.5; // 270°

  @override
  void paint(Canvas canvas, Size size) {
    final center = size.center(Offset.zero);
    final r = size.width / 2 - 3;

    final track = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 3
      ..strokeCap = StrokeCap.round
      ..color = Colors.white12;
    canvas.drawArc(
      Rect.fromCircle(center: center, radius: r),
      _startAngle,
      _sweep,
      false,
      track,
    );

    // Active fill from the unity midpoint toward the current value, so cuts and
    // boosts read as opposite-direction arcs from 12 o'clock.
    final mid = _startAngle + _sweep * _kCenter;
    final cur = _startAngle + _sweep * value;
    final fill = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 3
      ..strokeCap = StrokeCap.round
      ..color = _accent;
    canvas.drawArc(
      Rect.fromCircle(center: center, radius: r),
      math.min(mid, cur),
      (cur - mid).abs(),
      false,
      fill,
    );

    // Indicator line.
    final a = _startAngle + _sweep * value;
    final p = Paint()
      ..color = Colors.white70
      ..strokeWidth = 2
      ..strokeCap = StrokeCap.round;
    canvas.drawLine(
      center + Offset(math.cos(a), math.sin(a)) * (r * 0.35),
      center + Offset(math.cos(a), math.sin(a)) * r,
      p,
    );
  }

  @override
  bool shouldRepaint(_KnobPainter old) => old.value != value;
}

/// A vertical channel fader (0 at bottom .. 1 at top). Drag or tap to set;
/// double-tap to reset to unity.
class _VolumeFader extends StatelessWidget {
  final double value; // 0..1
  final ValueChanged<double> onChanged;
  final VoidCallback? onChangeEnd;
  const _VolumeFader({
    required this.value,
    required this.onChanged,
    this.onChangeEnd,
  });

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, c) {
        final h = c.maxHeight;
        void set(Offset p) {
          if (h <= 0) return;
          onChanged((1 - p.dy / h).clamp(0.0, 1.0));
        }

        return GestureDetector(
          behavior: HitTestBehavior.opaque,
          onPanDown: (d) => set(d.localPosition),
          onPanUpdate: (d) => set(d.localPosition),
          onPanEnd: (_) => onChangeEnd?.call(),
          onDoubleTap: () {
            onChanged(1.0);
            onChangeEnd?.call();
          },
          child: CustomPaint(
            size: const Size(double.infinity, double.infinity),
            painter: _FaderPainter(value),
          ),
        );
      },
    );
  }
}

class _FaderPainter extends CustomPainter {
  final double value; // 0..1
  _FaderPainter(this.value);

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    const pad = 6.0;
    final top = pad;
    final bot = size.height - pad;
    final y = bot - value * (bot - top);

    // Track.
    canvas.drawLine(
      Offset(cx, top),
      Offset(cx, bot),
      Paint()
        ..color = Colors.white12
        ..strokeWidth = 4
        ..strokeCap = StrokeCap.round,
    );
    // Filled portion below the handle.
    canvas.drawLine(
      Offset(cx, y),
      Offset(cx, bot),
      Paint()
        ..color = _accent
        ..strokeWidth = 4
        ..strokeCap = StrokeCap.round,
    );
    // Handle bar.
    final hw = size.width * 0.4;
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Rect.fromCenter(center: Offset(cx, y), width: hw * 2, height: 8),
        const Radius.circular(3),
      ),
      Paint()..color = Colors.white70,
    );
  }

  @override
  bool shouldRepaint(_FaderPainter old) => old.value != value;
}
