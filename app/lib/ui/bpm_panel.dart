// Per-deck BPM / beat-grid / metronome controls. Shows track BPM, the
// speed-adjusted ("effective") BPM, and a Rekordbox-style pitch percentage.
// When BPM is unknown it shows "???" with an emphasized pencil to enter it by
// hand. Offset can be nudged +/- and the metronome toggled per deck.
import 'package:flutter/material.dart';

import '../ffi/engine.dart';

const _accent = Color(0xFFE0344B);
const double _offsetNudgeMs = 5.0;

class BpmPanel extends StatelessWidget {
  final LazerdeckEngine engine;
  final int deck;
  final DeckState? state;

  const BpmPanel({
    super.key,
    required this.engine,
    required this.deck,
    required this.state,
  });

  @override
  Widget build(BuildContext context) {
    final s = state;
    final hasBpm = s?.hasBpm ?? false;

    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        SizedBox(
          width: 160,
          child: hasBpm ? _bpmReadout(s!) : _unknownReadout(),
        ),
        const SizedBox(width: 4),
        _pitchControl(s),
        const SizedBox(width: 8),
        _offsetControl(hasBpm ? s! : null),
        const SizedBox(width: 8),
        // Metronome toggle.
        _MiniButton(
          icon: Icons.timer_outlined,
          tooltip: 'Metronome',
          noBorder: true,
          lit: s?.metronomeEnabled ?? false,
          onTap: () =>
              engine.setMetronome(deck, !(s?.metronomeEnabled ?? false)),
        ),
        const SizedBox(width: 4),
        // Manual BPM/offset entry — emphasized when undetermined.
        _MiniButton(
          icon: Icons.edit,
          tooltip: 'Enter BPM & offset',
          noBorder: true,
          tint: hasBpm ? Colors.white54 : _accent,
          onTap: () => _editDialog(context),
        ),
      ],
    );
  }

  Widget _bpmReadout(DeckState s) {
    const orange = Color(0xFFFF9500);
    return Column(
      mainAxisAlignment: MainAxisAlignment.center,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          crossAxisAlignment: CrossAxisAlignment.baseline,
          textBaseline: TextBaseline.alphabetic,
          children: [
            Text(
              '${s.effectiveBpm.toStringAsFixed(1)}/${s.bpm.toStringAsFixed(1)}',
              style: const TextStyle(
                fontSize: 22,
                height: 1.0,
                fontFamily: 'bitroad',
                color: orange,
              ),
            ),
            const SizedBox(width: 3),
            const Text(
              'BPM',
              style: TextStyle(
                fontSize: 10,
                fontFamily: 'bitroad',
                color: Colors.white38,
              ),
            ),
          ],
        ),
      ],
    );
  }

  Widget _pitchControl(DeckState? s) {
    final pct = s?.speedPercent ?? 0.0;
    final adjusted = (s?.speed ?? 1.0) != 1.0;
    final sign = pct >= 0 ? '+' : '−';
    final pctStr = '$sign${pct.abs().toStringAsFixed(1)}%';
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        _MiniButton(
          child: _hybridIcon(Icons.remove, Icons.access_time),
          tooltip: 'Slower (−1 BPM)',
          size: 24,
          noBorder: true,
          onTap: () => engine.speedDown(deck),
        ),
        // Tap the percentage to reset tempo to 0%, or drag to adjust.
        Tooltip(
          message: 'Reset tempo (tap) / Adjust (drag)',
          child: GestureDetector(
            onVerticalDragUpdate: (details) {
              if (s == null) return;
              final next = (s.speed - details.delta.dy * 0.0005).clamp(0.001, 4.0);
              engine.setSpeed(deck, next);
            },
            child: InkWell(
              borderRadius: BorderRadius.circular(4),
              onTap: () => engine.resetSpeed(deck),
              child: SizedBox(
                width: 60,
                child: Text(
                  pctStr,
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                    fontFamily: 'bitroad',
                    color: adjusted ? _accent : Colors.white60,
                  ),
                ),
              ),
            ),
          ),
        ),
        _MiniButton(
          child: _hybridIcon(Icons.add, Icons.access_time),
          tooltip: 'Faster (+1 BPM)',
          size: 24,
          noBorder: true,
          onTap: () => engine.speedUp(deck),
        ),
      ],
    );
  }

  Widget _unknownReadout() {
    return Column(
      mainAxisAlignment: MainAxisAlignment.center,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: const [
        Text('???  BPM',
            style: TextStyle(
                fontSize: 20,
                fontFamily: 'bitroad',
                fontWeight: FontWeight.w600,
                color: Colors.white38)),
        SizedBox(height: 2),
        Text('tap ✎ to set tempo',
            style: TextStyle(
                fontSize: 10, fontFamily: 'bitroad', color: _accent)),
      ],
    );
  }

  Widget _offsetControl(DeckState? s) {
    final label = s == null
        ? '???'
        : '${s.offsetMs >= 0 ? '+' : '−'}${s.offsetMs.abs().toStringAsFixed(0)}ms';
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        _MiniButton(
          child: _hybridIcon(Icons.remove, Icons.music_note),
          tooltip: '−${_offsetNudgeMs.toStringAsFixed(0)} ms',
          size: 24,
          noBorder: true,
          onTap: () => engine.nudgeBeatOffsetMs(deck, -_offsetNudgeMs),
        ),
        SizedBox(
          width: 56,
          child: Text(
            label,
            textAlign: TextAlign.center,
            style: const TextStyle(
                fontSize: 11, fontFamily: 'bitroad', color: Colors.white70),
          ),
        ),
        _MiniButton(
          child: _hybridIcon(Icons.add, Icons.music_note),
          tooltip: '+${_offsetNudgeMs.toStringAsFixed(0)} ms',
          size: 24,
          noBorder: true,
          onTap: () => engine.nudgeBeatOffsetMs(deck, _offsetNudgeMs),
        ),
      ],
    );
  }

  Widget _hybridIcon(IconData base, IconData type) {
    return Stack(
      alignment: Alignment.center,
      children: [
        Icon(base, size: 18, color: Colors.white60),
        Positioned(
          right: -2,
          bottom: -2,
          child: Icon(type, size: 10, color: Colors.white38),
        ),
      ],
    );
  }

  Future<void> _editDialog(BuildContext context) async {
    final s = state;
    final letter = String.fromCharCode(0x41 + deck);
    final bpmCtl = TextEditingController(
        text: (s != null && s.hasBpm) ? s.bpm.toStringAsFixed(1) : '');
    final offCtl = TextEditingController(
        text: (s != null && s.hasBpm) ? s.offsetMs.toStringAsFixed(0) : '0');

    final apply = await showDialog<bool>(
      context: context,
      builder: (_) => AlertDialog(
        title: Text('Deck $letter — tempo'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextField(
              controller: bpmCtl,
              autofocus: true,
              keyboardType: const TextInputType.numberWithOptions(decimal: true),
              decoration: const InputDecoration(
                labelText: 'BPM',
                hintText: 'e.g. 128',
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: offCtl,
              keyboardType: const TextInputType.numberWithOptions(
                  decimal: true, signed: true),
              decoration: const InputDecoration(
                labelText: 'Beat offset (ms)',
                hintText: '0',
              ),
            ),
            const SizedBox(height: 16),
            Align(
              alignment: Alignment.centerLeft,
              child: TextButton.icon(
                style: TextButton.styleFrom(foregroundColor: _accent),
                icon: const Icon(Icons.refresh, size: 18),
                label: const Text('Delete from database & re-analyze'),
                onPressed: (s != null && s.hasTrack)
                    ? () {
                        engine.reanalyze(deck);
                        Navigator.of(context).pop(false);
                      }
                    : null,
              ),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('Apply'),
          ),
        ],
      ),
    );

    if (apply != true) return;
    final bpm = double.tryParse(bpmCtl.text.trim());
    final offMs = double.tryParse(offCtl.text.trim()) ?? 0.0;
    if (bpm != null && bpm > 0) {
      engine.setBpm(deck, bpm);
      final sr = state?.sampleRate ?? 0;
      if (sr > 0) engine.setBeatOffset(deck, offMs / 1000.0 * sr);
    }
  }
}

class _MiniButton extends StatelessWidget {
  final IconData? icon;
  final Widget? child;
  final String tooltip;
  final VoidCallback onTap;
  final bool lit;
  final Color? tint;
  final double size;
  final bool noBorder;

  const _MiniButton({
    this.icon,
    this.child,
    required this.tooltip,
    required this.onTap,
    this.lit = false,
    this.tint,
    this.size = 28,
    this.noBorder = false,
  });

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: tooltip,
      child: InkWell(
        borderRadius: BorderRadius.circular(6),
        onTap: onTap,
        child: Container(
          width: size,
          height: size,
          decoration: BoxDecoration(
            borderRadius: BorderRadius.circular(6),
            color: lit ? const Color(0x22E0344B) : Colors.transparent,
            border: noBorder
                ? null
                : Border.all(
                    color: lit ? const Color(0x88E0344B) : Colors.white12),
          ),
          child: Center(
            child: child ??
                Icon(
                  icon,
                  size: size * 0.55,
                  color: lit ? _accent : (tint ?? Colors.white60),
                ),
          ),
        ),
      ),
    );
  }
}
