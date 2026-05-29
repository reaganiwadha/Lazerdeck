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
        hasBpm ? _bpmReadout(s!) : _unknownReadout(),
        const SizedBox(width: 14),
        _pitchControl(s),
        const SizedBox(width: 12),
        _offsetControl(hasBpm ? s! : null),
        const SizedBox(width: 10),
        // Metronome toggle.
        _MiniButton(
          icon: Icons.av_timer,
          tooltip: 'Metronome',
          lit: s?.metronomeEnabled ?? false,
          onTap: () =>
              engine.setMetronome(deck, !(s?.metronomeEnabled ?? false)),
        ),
        const SizedBox(width: 4),
        // Manual BPM/offset entry — emphasized when undetermined.
        _MiniButton(
          icon: Icons.edit,
          tooltip: 'Enter BPM & offset',
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
              '${s.bpm.toStringAsFixed(1)}/${s.effectiveBpm.toStringAsFixed(1)}',
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
    return Column(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        const Text('PITCH',
            style: TextStyle(
                fontSize: 8, letterSpacing: 1.5, color: Colors.white38)),
        const SizedBox(height: 1),
        Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            _MiniButton(
              icon: Icons.remove,
              tooltip: 'Slower (−1 BPM)',
              size: 24,
              onTap: () => engine.speedDown(deck),
            ),
            // Tap the percentage to reset tempo to 0%.
            Tooltip(
              message: 'Reset tempo',
              child: InkWell(
                borderRadius: BorderRadius.circular(4),
                onTap: () => engine.resetSpeed(deck),
                child: SizedBox(
                  width: 52,
                  child: Text(
                    pctStr,
                    textAlign: TextAlign.center,
                    style: TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.w600,
                      fontFamily: 'monospace',
                      color: adjusted ? _accent : Colors.white60,
                    ),
                  ),
                ),
              ),
            ),
            _MiniButton(
              icon: Icons.add,
              tooltip: 'Faster (+1 BPM)',
              size: 24,
              onTap: () => engine.speedUp(deck),
            ),
          ],
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
                fontSize: 20, fontWeight: FontWeight.w600, color: Colors.white38)),
        SizedBox(height: 2),
        Text('tap ✎ to set tempo',
            style: TextStyle(fontSize: 10, color: _accent)),
      ],
    );
  }

  Widget _offsetControl(DeckState? s) {
    final label = s == null
        ? '???'
        : '${s.offsetMs >= 0 ? '+' : '−'}${s.offsetMs.abs().toStringAsFixed(0)}ms';
    return Column(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        const Text('OFFSET',
            style: TextStyle(
                fontSize: 8, letterSpacing: 1.5, color: Colors.white38)),
        const SizedBox(height: 1),
        Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            _MiniButton(
              icon: Icons.remove,
              tooltip: '−${_offsetNudgeMs.toStringAsFixed(0)} ms',
              size: 24,
              onTap: () => engine.nudgeBeatOffsetMs(deck, -_offsetNudgeMs),
            ),
            SizedBox(
              width: 48,
              child: Text(
                label,
                textAlign: TextAlign.center,
                style: const TextStyle(
                    fontSize: 11,
                    fontFamily: 'monospace',
                    color: Colors.white70),
              ),
            ),
            _MiniButton(
              icon: Icons.add,
              tooltip: '+${_offsetNudgeMs.toStringAsFixed(0)} ms',
              size: 24,
              onTap: () => engine.nudgeBeatOffsetMs(deck, _offsetNudgeMs),
            ),
          ],
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
  final IconData icon;
  final String tooltip;
  final VoidCallback onTap;
  final bool lit;
  final Color? tint;
  final double size;

  const _MiniButton({
    required this.icon,
    required this.tooltip,
    required this.onTap,
    this.lit = false,
    this.tint,
    this.size = 28,
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
            border: Border.all(
                color: lit ? const Color(0x88E0344B) : Colors.white12),
          ),
          child: Icon(
            icon,
            size: size * 0.55,
            color: lit ? _accent : (tint ?? Colors.white60),
          ),
        ),
      ),
    );
  }
}
