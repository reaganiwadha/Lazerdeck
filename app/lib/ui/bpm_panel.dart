// Per-deck BPM controls, split into two pieces so they can live in different
// bars: [BpmReadout] is the tempo text (shown in the title bar), and
// [BpmControls] is the pitch nudge / offset nudge / metronome / edit cluster
// (shown in the transport bar). Manual BPM/offset entry and "delete from DB &
// re-analyze" live in the edit dialog.
import 'package:flutter/material.dart';

import '../ffi/engine.dart';
import 'tap_tempo_wizard.dart';

const _accent = Color(0xFFE0344B);
const double _offsetNudgeMs = 5.0;

/// Tempo readout: speed-adjusted ("effective") BPM over the track BPM, or a
/// "???" prompt when the tempo is unknown.
class BpmReadout extends StatelessWidget {
  final DeckState? state;
  const BpmReadout({super.key, required this.state});

  @override
  Widget build(BuildContext context) {
    final s = state;
    final hasBpm = s?.hasBpm ?? false;
    return SizedBox(
      width: 168,
      child: hasBpm ? _bpmReadout(s!) : _unknownReadout(),
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
                fontSize: 24,
                height: 1.0,
                fontFamily: 'bitroad',
                color: orange,
              ),
            ),
            const SizedBox(width: 4),
            const Text(
              'BPM',
              style: TextStyle(
                fontSize: 11,
                fontFamily: 'bitroad',
                color: Colors.white38,
              ),
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
                fontSize: 22,
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
}

/// Tempo controls: pitch nudge, beat-offset nudge, metronome, and manual edit.
class BpmControls extends StatelessWidget {
  final LazerdeckEngine engine;
  final int deck;
  final DeckState? state;
  final bool isMaster;
  final VoidCallback onSetMaster;

  const BpmControls({
    super.key,
    required this.engine,
    required this.deck,
    required this.state,
    required this.isMaster,
    required this.onSetMaster,
  });

  @override
  Widget build(BuildContext context) {
    final s = state;
    final hasBpm = s?.hasBpm ?? false;

    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        _masterControl(),
        const SizedBox(width: 8),
        _syncControl(s),
        const SizedBox(width: 8),
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
        // Tap Tempo wizard.
        _MiniButton(
          icon: Icons.touch_app,
          tooltip: 'Tap Tempo wizard',
          noBorder: true,
          onTap: () => showTapTempoWizard(context, engine, deck),
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

  Widget _masterControl() {
    final active = isMaster;
    const masterColor = Color(0xFF00E6FF); // Premium electric cyan/teal

    return _MiniButton(
      tooltip: active
          ? 'This deck is the TEMPO MASTER'
          : 'Set this deck as TEMPO MASTER',
      noBorder: false,
      lit: active,
      litColor: masterColor,
      size: 28,
      width: 52,
      onTap: onSetMaster,
      child: Text('MASTER',
          style: TextStyle(
            fontFamily: 'bitroad',
            fontSize: 9,
            fontWeight: FontWeight.w600,
            color: active ? masterColor : Colors.white38,
          )),
    );
  }

  Widget _syncControl(DeckState? s) {
    final active = s?.syncActive ?? false;
    final otherDeck = deck == 0 ? 1 : 0; // simple heuristic for 2 decks

    return _MiniButton(
      tooltip: active
          ? 'Sync Active (Source: Deck ${String.fromCharCode(0x41 + (s?.syncSource ?? 0))})'
          : 'Sync to Deck ${String.fromCharCode(0x41 + otherDeck)}',
      noBorder: false,
      lit: active,
      size: 28,
      width: 42,
      onTap: () {
        if (active) {
          engine.setSync(deck, -1);
        } else {
          engine.setSync(deck, otherDeck);
        }
      },
      child: Text('SYNC',
          style: TextStyle(
            fontFamily: 'bitroad',
            fontSize: 10,
            fontWeight: FontWeight.w600,
            color: active ? _accent : Colors.white60,
          )),
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
          tooltip: 'Slower (−1 BPM)',
          size: 24,
          noBorder: true,
          onTap: () => engine.speedDown(deck),
          child: _hybridIcon(Icons.remove, Icons.access_time),
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
          tooltip: 'Faster (+1 BPM)',
          size: 24,
          noBorder: true,
          onTap: () => engine.speedUp(deck),
          child: _hybridIcon(Icons.add, Icons.access_time),
        ),
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
          tooltip: '−${_offsetNudgeMs.toStringAsFixed(0)} ms',
          size: 24,
          noBorder: true,
          onTap: () => engine.nudgeBeatOffsetMs(deck, -_offsetNudgeMs),
          child: _hybridIcon(Icons.remove, Icons.music_note),
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
          tooltip: '+${_offsetNudgeMs.toStringAsFixed(0)} ms',
          size: 24,
          noBorder: true,
          onTap: () => engine.nudgeBeatOffsetMs(deck, _offsetNudgeMs),
          child: _hybridIcon(Icons.add, Icons.music_note),
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
  final double? width;
  final bool noBorder;
  final Color? litColor;

  const _MiniButton({
    this.icon,
    this.child,
    required this.tooltip,
    required this.onTap,
    this.lit = false,
    this.tint,
    this.size = 28,
    this.width,
    this.noBorder = false,
    this.litColor,
  });

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: tooltip,
      child: InkWell(
        borderRadius: BorderRadius.circular(6),
        onTap: onTap,
        child: Container(
          width: width ?? size,
          height: size,
          decoration: BoxDecoration(
            borderRadius: BorderRadius.circular(6),
            color: lit
                ? (litColor?.withValues(alpha: 0.13) ?? const Color(0x22E0344B))
                : Colors.transparent,
            border: noBorder
                ? null
                : Border.all(
                    color: lit
                        ? (litColor?.withValues(alpha: 0.53) ?? const Color(0x88E0344B))
                        : Colors.white12),
          ),
          child: Center(
            child: child ??
                Icon(
                  icon,
                  size: size * 0.55,
                  color: lit ? (litColor ?? _accent) : (tint ?? Colors.white60),
                ),
          ),
        ),
      ),
    );
  }
}
