// Per-deck 3-band channel EQ: three small knobs (HI / MID / LOW) that send
// DJM-style EQ commands to the engine. Each knob is 0..1 with center = unity.
import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../ffi/engine.dart';

const _kCenter = 0.5; // unity / 0 dB

class EqPanel extends StatefulWidget {
  final LazerdeckEngine engine;
  final int deck;
  const EqPanel({super.key, required this.engine, required this.deck});

  @override
  State<EqPanel> createState() => _EqPanelState();
}

class _EqPanelState extends State<EqPanel> {
  double _hi = _kCenter;
  double _mid = _kCenter;
  double _low = _kCenter;

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        _EqKnob(
          label: 'HI',
          value: _hi,
          onChanged: (v) {
            setState(() => _hi = v);
            widget.engine.setEqHigh(widget.deck, v);
          },
        ),
        _EqKnob(
          label: 'MID',
          value: _mid,
          onChanged: (v) {
            setState(() => _mid = v);
            widget.engine.setEqMid(widget.deck, v);
          },
        ),
        _EqKnob(
          label: 'LOW',
          value: _low,
          onChanged: (v) {
            setState(() => _low = v);
            widget.engine.setEqLow(widget.deck, v);
          },
        ),
      ],
    );
  }
}

/// A small rotary knob. Drag vertically to turn; double-tap to recenter.
class _EqKnob extends StatelessWidget {
  final String label;
  final double value; // 0..1
  final ValueChanged<double> onChanged;

  const _EqKnob({
    required this.label,
    required this.value,
    required this.onChanged,
  });

  // Pixels of vertical drag for the full 0..1 sweep.
  static const _dragRange = 160.0;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 4),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          GestureDetector(
            behavior: HitTestBehavior.opaque,
            onDoubleTap: () => onChanged(_kCenter),
            onVerticalDragUpdate: (d) {
              final next = (value - d.delta.dy / _dragRange).clamp(0.0, 1.0);
              if (next != value) onChanged(next);
            },
            child: SizedBox(
              width: 38,
              height: 38,
              child: CustomPaint(painter: _KnobPainter(value)),
            ),
          ),
          const SizedBox(height: 2),
          Text(
            label,
            style: const TextStyle(
              fontSize: 9,
              fontFamily: 'bitroad',
              letterSpacing: 1,
              fontWeight: FontWeight.w600,
              color: Colors.white38,
            ),
          ),
        ],
      ),
    );
  }
}

class _KnobPainter extends CustomPainter {
  final double value; // 0..1
  _KnobPainter(this.value);

  static const _accent = Color(0xFFE0344B);

  // Sweep from 7 o'clock to 5 o'clock (300° total), leaving a gap at the bottom.
  static const _startAngle = math.pi * 0.75; // 135°
  static const _sweep = math.pi * 1.5; // 270°

  @override
  void paint(Canvas canvas, Size size) {
    final center = size.center(Offset.zero);
    final r = size.width / 2 - 3;

    // Track.
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

    // Active fill from center (unity) toward the current value, so cuts and
    // boosts read as opposite-direction arcs from the 12 o'clock midpoint.
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
