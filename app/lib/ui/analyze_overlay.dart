// Small status pill shown over a deck's waveform: a spinner while the engine is
// detecting BPM, and a transient "analysis failed" warning (5 s) if detection
// finished without producing a tempo.
import 'dart:async';

import 'package:flutter/material.dart';

const _txt = TextStyle(
  fontSize: 11,
  fontFamily: 'bitroad',
  fontWeight: FontWeight.w600,
  color: Colors.white,
);

class AnalyzeOverlay extends StatefulWidget {
  final bool isAnalyzing;
  final bool hasBpm;
  final String trackId; // resets the warning when the track changes

  const AnalyzeOverlay({
    super.key,
    required this.isAnalyzing,
    required this.hasBpm,
    required this.trackId,
  });

  @override
  State<AnalyzeOverlay> createState() => _AnalyzeOverlayState();
}

class _AnalyzeOverlayState extends State<AnalyzeOverlay> {
  bool _showFailed = false;
  Timer? _failTimer;

  @override
  void didUpdateWidget(AnalyzeOverlay old) {
    super.didUpdateWidget(old);

    // New track — clear any stale warning.
    if (old.trackId != widget.trackId) {
      _clearFailed();
      return;
    }

    // Analysis just started — hide a previous failure.
    if (!old.isAnalyzing && widget.isAnalyzing) {
      _clearFailed();
      return;
    }

    // Analysis just finished — warn for 5 s if it produced no tempo.
    if (old.isAnalyzing && !widget.isAnalyzing) {
      if (!widget.hasBpm) {
        _failTimer?.cancel();
        setState(() => _showFailed = true);
        _failTimer = Timer(const Duration(seconds: 5), () {
          if (mounted) setState(() => _showFailed = false);
        });
      } else {
        _clearFailed();
      }
    }
  }

  void _clearFailed() {
    _failTimer?.cancel();
    if (_showFailed && mounted) setState(() => _showFailed = false);
  }

  @override
  void dispose() {
    _failTimer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (widget.isAnalyzing) {
      return const _Pill(
        color: Color(0xCC1A1A1A),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            SizedBox(
              width: 12,
              height: 12,
              child: CircularProgressIndicator(
                strokeWidth: 2,
                valueColor: AlwaysStoppedAnimation(Color(0xFFFF9500)),
              ),
            ),
            SizedBox(width: 8),
            Text('Analyzing BPM…', style: _txt),
          ],
        ),
      );
    }

    if (_showFailed) {
      return const _Pill(
        color: Color(0xCC3A1414),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.warning_amber_rounded, size: 14, color: Color(0xFFE0344B)),
            SizedBox(width: 6),
            Text('BPM analysis failed', style: _txt),
          ],
        ),
      );
    }

    return const SizedBox.shrink();
  }
}

class _Pill extends StatelessWidget {
  final Color color;
  final Widget child;
  const _Pill({required this.color, required this.child});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      decoration: BoxDecoration(
        color: color,
        borderRadius: BorderRadius.circular(6),
      ),
      child: child,
    );
  }
}
