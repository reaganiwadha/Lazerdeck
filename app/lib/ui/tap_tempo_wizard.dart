// Tap Tempo wizard: a popup that derives BPM and/or the beat-grid offset from
// spacebar taps. Two independent toggles choose what each SPACE tap feeds
// (either, both, or neither):
//   • Tap BPM    — tempo comes from the tap intervals.
//   • Tap Offset — tap on the beat while the track plays; the grid phase comes
//                  from where the taps land in the track.
// With both on, one tap on the beat sets tempo and phase at once. Each tap
// fires a one-shot metronome click via the engine so taps are audible.
// The resulting BPM and offset land in editable text fields, so they can be
// fine-tuned by hand before storing. A "directly replace" toggle applies the
// values to the deck automatically once tapping is stable (and runs the grid
// metronome); otherwise the user stores them with the button.
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../ffi/engine.dart';

const _accent = Color(0xFFE0344B);

// A gap longer than this between taps starts a fresh measurement.
const _resetGapMs = 2000;
// Minimum BPM taps before the result is "good enough" to auto-replace.
const _minBpmTaps = 4;
// Minimum offset taps before the result is "good enough" to auto-replace.
const _minOffsetTaps = 2;
// Max BPM-interval coefficient-of-variation for a "good enough" auto-replace.
const _stableCv = 0.06;

enum _TapMode { bpm, offset }

Future<void> showTapTempoWizard(
  BuildContext context,
  LazerdeckEngine engine,
  int deck,
) {
  return showDialog<void>(
    context: context,
    builder: (_) => _TapTempoWizard(engine: engine, deck: deck),
  );
}

class _TapTempoWizard extends StatefulWidget {
  final LazerdeckEngine engine;
  final int deck;
  const _TapTempoWizard({required this.engine, required this.deck});

  @override
  State<_TapTempoWizard> createState() => _TapTempoWizardState();
}

class _TapTempoWizardState extends State<_TapTempoWizard> {
  final FocusNode _focus = FocusNode();
  final Stopwatch _clock = Stopwatch()..start();

  final TextEditingController _bpmCtl = TextEditingController();
  final TextEditingController _offCtl = TextEditingController(); // milliseconds

  // Which targets each tap feeds — independent, so both can be on at once.
  Set<_TapMode> _modes = {_TapMode.bpm, _TapMode.offset};

  // BPM tapping: tap timestamps (ms on _clock).
  final List<int> _bpmTapMs = [];
  // Offset tapping: deck frame each on-beat tap landed on.
  final List<int> _offTapFrames = [];

  int _sampleRate = 0;
  bool _directlyReplace = false;

  @override
  void initState() {
    super.initState();
    // Seed the editable fields from the deck's current analysis.
    final s = widget.engine.deckState(widget.deck);
    if (s != null) {
      _sampleRate = s.sampleRate;
      if (s.hasBpm) {
        _bpmCtl.text = s.bpm.toStringAsFixed(2);
        _offCtl.text = s.offsetMs.toStringAsFixed(0);
      }
    }
    // Opening the wizard silences the grid metronome; taps then click manually.
    widget.engine.setMetronome(widget.deck, false);
  }

  @override
  void dispose() {
    _focus.dispose();
    _bpmCtl.dispose();
    _offCtl.dispose();
    super.dispose();
  }

  // ---- working values (text fields are the source of truth) ---------------

  double? get _bpmValue {
    final v = double.tryParse(_bpmCtl.text.trim());
    return (v != null && v > 0) ? v : null;
  }

  double? get _offMsValue => double.tryParse(_offCtl.text.trim());

  double get _framesPerBeat {
    final b = _bpmValue;
    if (b == null || _sampleRate <= 0) return 0;
    return _sampleRate * 60.0 / b;
  }

  List<int> get _bpmIntervals {
    final out = <int>[];
    for (var i = 1; i < _bpmTapMs.length; i++) {
      out.add(_bpmTapMs[i] - _bpmTapMs[i - 1]);
    }
    return out;
  }

  /// BPM-interval coefficient of variation (stddev / mean); lower = steadier.
  double? get _bpmCv {
    final iv = _bpmIntervals;
    if (iv.length < 2) return null;
    final mean = iv.reduce((a, b) => a + b) / iv.length;
    if (mean <= 0) return null;
    final variance =
        iv.map((x) => (x - mean) * (x - mean)).reduce((a, b) => a + b) /
            iv.length;
    return math.sqrt(variance) / mean;
  }

  bool get _bpmSteady {
    final cv = _bpmCv;
    return _bpmTapMs.length >= _minBpmTaps && cv != null && cv <= _stableCv;
  }

  bool get _offsetReady =>
      _offTapFrames.length >= _minOffsetTaps && _framesPerBeat > 0;

  bool get _bpmOn => _modes.contains(_TapMode.bpm);
  bool get _offsetOn => _modes.contains(_TapMode.offset);

  /// Whether every enabled mode has produced a stable-enough result to
  /// auto-apply (and at least one mode is enabled).
  bool get _readyForReplace =>
      _modes.isNotEmpty &&
      (!_bpmOn || _bpmSteady) &&
      (!_offsetOn || _offsetReady);

  bool get _canStore => _bpmValue != null;

  // ---- tapping ------------------------------------------------------------

  void _tap() {
    // Every tap is audible regardless of mode.
    widget.engine.metronomeTick(widget.deck);
    // BPM first so the offset pass can use the freshly tapped tempo.
    if (_bpmOn) _tapBpm();
    if (_offsetOn) _tapOffset();
    if (_directlyReplace && _readyForReplace) _store();
    setState(() {});
  }

  void _tapBpm() {
    final now = _clock.elapsedMilliseconds;
    if (_bpmTapMs.isNotEmpty && now - _bpmTapMs.last > _resetGapMs) {
      _bpmTapMs.clear();
    }
    _bpmTapMs.add(now);

    final s = widget.engine.deckState(widget.deck);
    if (s != null) _sampleRate = s.sampleRate;

    final iv = _bpmIntervals;
    if (iv.isNotEmpty) {
      final sorted = [...iv]..sort();
      final medianMs = sorted[sorted.length ~/ 2].toDouble();
      if (medianMs > 0) _bpmCtl.text = (60000.0 / medianMs).toStringAsFixed(2);
    }
  }

  void _tapOffset() {
    final s = widget.engine.deckState(widget.deck);
    if (s == null) return;
    _sampleRate = s.sampleRate;

    // Offset taps only mean something against a moving, tempo-known track.
    if (!s.isPlaying || _framesPerBeat <= 0) return;

    if (_offTapFrames.isNotEmpty) {
      // A long pause (in track time) starts a fresh offset measurement.
      final gapFrames = s.currentFrame - _offTapFrames.last;
      if (gapFrames.abs() > _resetGapMs / 1000.0 * _sampleRate) {
        _offTapFrames.clear();
      }
    }
    _offTapFrames.add(s.currentFrame);

    // Circular mean of tap positions modulo one beat period, so taps either
    // side of the 0/fpb wrap don't average to the wrong phase.
    final fpb = _framesPerBeat;
    double sumSin = 0, sumCos = 0;
    for (final f in _offTapFrames) {
      final theta = 2 * math.pi * (f % fpb) / fpb;
      sumSin += math.sin(theta);
      sumCos += math.cos(theta);
    }
    var mean = math.atan2(sumSin, sumCos);
    if (mean < 0) mean += 2 * math.pi;
    final offFrames = mean / (2 * math.pi) * fpb;
    _offCtl.text = (offFrames / _sampleRate * 1000.0).toStringAsFixed(0);
  }

  // ---- actions ------------------------------------------------------------

  void _store() {
    final b = _bpmValue;
    if (b == null) return;
    widget.engine.setBpm(widget.deck, b);
    final ms = _offMsValue;
    if (ms != null && _sampleRate > 0) {
      widget.engine.setBeatOffset(widget.deck, ms / 1000.0 * _sampleRate);
    }
  }

  void _reset() {
    setState(() {
      if (_bpmOn) _bpmTapMs.clear();
      if (_offsetOn) _offTapFrames.clear();
    });
    _focus.requestFocus();
  }

  void _setModes(Set<_TapMode> m) {
    setState(() => _modes = m);
    _focus.requestFocus();
  }

  void _toggleDirectlyReplace(bool v) {
    setState(() => _directlyReplace = v);
    // Enabling direct replace turns the grid metronome on so you hear the grid
    // line up against the track; disabling silences it again.
    widget.engine.setMetronome(widget.deck, v);
    if (v && _readyForReplace) _store();
    _focus.requestFocus();
  }

  KeyEventResult _onKey(FocusNode node, KeyEvent event) {
    if (event is KeyDownEvent &&
        event.logicalKey == LogicalKeyboardKey.space) {
      _tap();
      return KeyEventResult.handled;
    }
    return KeyEventResult.ignored;
  }

  // ---- ui -----------------------------------------------------------------

  @override
  Widget build(BuildContext context) {
    final letter = String.fromCharCode(0x41 + widget.deck);

    return Focus(
      focusNode: _focus,
      autofocus: true,
      onKeyEvent: _onKey,
      child: AlertDialog(
        title: Text('Deck $letter — Tap Tempo Wizard'),
        content: SizedBox(
          width: 380,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              SegmentedButton<_TapMode>(
                multiSelectionEnabled: true,
                emptySelectionAllowed: true,
                segments: const [
                  ButtonSegment(
                    value: _TapMode.bpm,
                    icon: Icon(Icons.graphic_eq),
                    label: Text('Tap BPM'),
                  ),
                  ButtonSegment(
                    value: _TapMode.offset,
                    icon: Icon(Icons.adjust),
                    label: Text('Tap Offset'),
                  ),
                ],
                selected: _modes,
                onSelectionChanged: _setModes,
              ),
              const SizedBox(height: 14),
              _tapPad(),
              const SizedBox(height: 16),
              Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Expanded(
                    child: TextField(
                      controller: _bpmCtl,
                      keyboardType: const TextInputType.numberWithOptions(
                          decimal: true),
                      inputFormatters: [
                        FilteringTextInputFormatter.allow(RegExp(r'[0-9.]')),
                      ],
                      onChanged: (_) => setState(() {}),
                      decoration: const InputDecoration(
                        labelText: 'BPM',
                        isDense: true,
                      ),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: TextField(
                      controller: _offCtl,
                      keyboardType: const TextInputType.numberWithOptions(
                          decimal: true, signed: true),
                      inputFormatters: [
                        FilteringTextInputFormatter.allow(RegExp(r'[0-9.\-]')),
                      ],
                      onChanged: (_) => setState(() {}),
                      decoration: const InputDecoration(
                        labelText: 'Offset (ms)',
                        isDense: true,
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 4),
              SwitchListTile(
                contentPadding: EdgeInsets.zero,
                activeThumbColor: _accent,
                title: const Text('Directly replace'),
                subtitle: const Text(
                  'Auto-apply once steady (and run the metronome)',
                  style: TextStyle(fontSize: 11, color: Colors.white38),
                ),
                value: _directlyReplace,
                onChanged: _toggleDirectlyReplace,
              ),
            ],
          ),
        ),
        actions: [
          TextButton(
            onPressed: _reset,
            child: const Text('Reset'),
          ),
          TextButton(
            onPressed: () => Navigator.of(context).pop(),
            child: const Text('Close'),
          ),
          FilledButton(
            style: FilledButton.styleFrom(backgroundColor: _accent),
            onPressed: _canStore
                ? () {
                    _store();
                    _focus.requestFocus();
                  }
                : null,
            child: const Text('Store'),
          ),
        ],
      ),
    );
  }

  Widget _tapPad() {
    final String label;
    if (!_bpmOn && !_offsetOn) {
      label = 'Enable Tap BPM or Tap Offset above';
    } else if (_offsetOn && !_bpmOn && _bpmValue == null) {
      label = 'Set BPM first';
    } else {
      final parts = <String>[];
      if (_bpmOn) parts.add('BPM ${_bpmTapMs.length}');
      if (_offsetOn) parts.add('offset ${_offTapFrames.length}');
      final tapped = _bpmTapMs.isNotEmpty || _offTapFrames.isNotEmpty;
      final verb = _offsetOn ? 'on the beat' : 'in time';
      label = tapped ? parts.join('  ·  ') : 'Tap SPACE $verb';
    }

    final steady = _readyForReplace;
    return GestureDetector(
      onTap: () {
        _tap();
        _focus.requestFocus();
      },
      child: Container(
        height: 72,
        decoration: BoxDecoration(
          borderRadius: BorderRadius.circular(8),
          border: Border.all(color: steady ? _accent : Colors.white24),
          color: const Color(0x11FFFFFF),
        ),
        child: Center(
          child: Text(
            label,
            style: const TextStyle(
                fontSize: 16, fontFamily: 'bitroad', color: Colors.white70),
          ),
        ),
      ),
    );
  }
}
