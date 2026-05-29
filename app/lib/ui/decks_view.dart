import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';

import '../ffi/engine.dart';
import 'analyze_overlay.dart';
import 'bpm_panel.dart';
import 'cover_art.dart';
import 'eq_panel.dart';
import 'settings_dialog.dart';
import 'waveform_view.dart';

class DecksView extends StatefulWidget {
  final LazerdeckEngine engine;
  const DecksView({super.key, required this.engine});

  @override
  State<DecksView> createState() => _DecksViewState();
}

class _DecksViewState extends State<DecksView> {
  Timer? _poll;
  List<DeckState?> _states = const [];

  @override
  void initState() {
    super.initState();
    _states = List.filled(widget.engine.deckCount, null);
    _poll = Timer.periodic(const Duration(milliseconds: 16), (_) => _refresh());
  }

  void _refresh() {
    final next = <DeckState?>[
      for (var i = 0; i < widget.engine.deckCount; i++)
        widget.engine.deckState(i),
    ];
    if (mounted) setState(() => _states = next);
  }

  @override
  void dispose() {
    _poll?.cancel();
    widget.engine.dispose();
    super.dispose();
  }

  Future<void> _openFile(int deck) async {
    final result = await FilePicker.pickFiles(type: FileType.audio);
    final path = result?.files.single.path;
    if (path != null) widget.engine.loadFile(deck, path);
  }

  @override
  Widget build(BuildContext context) {
    final count = widget.engine.deckCount;
    return Scaffold(
      body: Stack(
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // Stacked deck panels (A on top, B below).
              Expanded(
                child: Column(
                  children: [
                    for (var i = 0; i < count; i++)
                      Expanded(
                        child: _DeckPanel(
                          engine: widget.engine,
                          index: i,
                          state: i < _states.length ? _states[i] : null,
                          onOpen: () => _openFile(i),
                        ),
                      ),
                  ],
                ),
              ),
              // Signal routing diagram down the right edge.
              SizedBox(
                width: 132,
                child: _RoutingDiagram(deckCount: count, states: _states),
              ),
            ],
          ),
          // Unobtrusive settings access (no app bar).
          Positioned(
            top: 6,
            right: 8,
            child: IconButton(
              tooltip: 'Audio settings',
              iconSize: 18,
              color: Colors.white24,
              onPressed: () => showAudioSettings(context, widget.engine),
              icon: const Icon(Icons.settings),
            ),
          ),
        ],
      ),
    );
  }
}

/// One deck: an edge-to-edge waveform with a control strip beneath it
/// (huge letter, track title, tape-style transport, timecode).
class _DeckPanel extends StatelessWidget {
  final LazerdeckEngine engine;
  final int index;
  final DeckState? state;
  final VoidCallback onOpen;

  const _DeckPanel({
    required this.engine,
    required this.index,
    required this.state,
    required this.onOpen,
  });

  static String _basename(String p) {
    final i = p.lastIndexOf(RegExp(r'[\\/]'));
    return i >= 0 ? p.substring(i + 1) : p;
  }

  static String _timecode(Duration d) {
    final m = d.inMinutes.remainder(60).toString().padLeft(2, '0');
    final s = d.inSeconds.remainder(60).toString().padLeft(2, '0');
    final cs =
        (d.inMilliseconds.remainder(1000) ~/ 10).toString().padLeft(2, '0');
    return '$m:$s.$cs';
  }

  void _cmd(String action) => engine.pushCommand('\$d${index + 1} $action');

  @override
  Widget build(BuildContext context) {
    final s = state;
    final playing = s?.isPlaying ?? false;
    final letter = String.fromCharCode('A'.codeUnitAt(0) + index);
    final hasTrack = s != null && s.hasTrack;

    return Padding(
      padding: const EdgeInsets.fromLTRB(0, 6, 8, 6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // Title bar, waveform, and transport bar all share the waveform's
          // width. Only the FX and mixer columns sit outside it (to the right).
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                // Title bar: cover art + title/artist, tempo readout, and the
                // bar.beat + timecode position. Tap the cover/title to load.
                SizedBox(
                  height: 84,
                  child: Row(
                    children: [
                      Expanded(
                        child: InkWell(
                          onTap: onOpen,
                          borderRadius: BorderRadius.circular(6),
                          child: Padding(
                            padding: const EdgeInsets.symmetric(
                                horizontal: 8, vertical: 6),
                            child: DeckTrackHeader(
                              path: hasTrack ? s.filepath : null,
                              fallbackName:
                                  hasTrack ? _basename(s.filepath) : '',
                              coverSize: 72,
                            ),
                          ),
                        ),
                      ),
                      const SizedBox(width: 16),
                      BpmReadout(state: s),
                      const SizedBox(width: 16),
                      _positionReadout(s, playing),
                      const SizedBox(width: 4),
                    ],
                  ),
                ),
                const SizedBox(height: 8),
                // Edge-to-edge waveform, thin framed like the old SDL view, with
                // the "Deck A/B" label overlaid in the top-left corner.
                Expanded(
                  child: DecoratedBox(
                    decoration: BoxDecoration(
                      border: Border.all(
                        color:
                            playing ? const Color(0x55E0344B) : Colors.white12,
                      ),
                    ),
                    child: Stack(
                      fit: StackFit.expand,
                      children: [
                        WaveformView(engine: engine, deck: index),
                        Positioned(
                          top: 6,
                          left: 10,
                          child: StrokedText(
                            'Deck $letter',
                            fontSize: 17,
                            fontFamily: 'bitroad',
                            fontWeight: FontWeight.w700,
                            letterSpacing: 1,
                            strokeWidth: 3,
                          ),
                        ),
                        Positioned(
                          top: 6,
                          left: 0,
                          right: 0,
                          child: Center(
                            child: AnalyzeOverlay(
                              isAnalyzing: s?.isAnalyzing ?? false,
                              hasBpm: s?.hasBpm ?? false,
                              trackId: s?.filepath ?? '',
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
                const SizedBox(height: 8),
                // Transport bar, centered: playback, loop, tempo controls.
                SizedBox(
                  height: 56,
                  child: Row(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      _TransportBar(
                        isPlaying: playing,
                        onRec: onOpen,
                        onRew: () => _cmd('seek -4'),
                        onPlay: () => engine.play(index),
                        onFfwd: () => _cmd('seek 4'),
                        onStop: () => _cmd('stop'),
                        onPause: () => engine.pause(index),
                      ),
                      const SizedBox(width: 12),
                      _LoopBar(
                        state: s,
                        onRecall: () => engine.reloop(index),
                        onA: () => engine.loopIn(index),
                        onB: () => engine.loopOut(index),
                        onExit: () => engine.clearLoop(index),
                      ),
                      const SizedBox(width: 14),
                      BpmControls(engine: engine, deck: index, state: s),
                    ],
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(width: 8),
          // EQ + FX, vertically bounded to the waveform: the spacers match the
          // title bar (84 + 8) above and the transport bar (8 + 56) below, so
          // this column's Expanded lines up exactly with the waveform's.
          Column(
            children: [
              const SizedBox(height: 92),
              Expanded(
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    MixerBox(engine: engine, deck: index),
                    const SizedBox(width: 8),
                    const FxBox(),
                  ],
                ),
              ),
              const SizedBox(height: 64),
            ],
          ),
        ],
      ),
    );
  }

  // bar.beat counter over the elapsed timecode, right-aligned. Lives in the
  // title bar.
  Widget _positionReadout(DeckState? s, bool playing) {
    return Column(
      mainAxisAlignment: MainAxisAlignment.center,
      crossAxisAlignment: CrossAxisAlignment.end,
      children: [
        SizedBox(
          width: 140,
          child: Text(
            s?.barBeat ?? '—.—',
            textAlign: TextAlign.right,
            style: TextStyle(
              fontSize: 26,
              height: 1.0,
              fontFamily: 'bitroad',
              fontWeight: FontWeight.w600,
              letterSpacing: 1,
              color: playing ? const Color(0xFFE0344B) : Colors.white,
            ),
          ),
        ),
        const SizedBox(height: 2),
        SizedBox(
          width: 140,
          child: Text(
            _timecode(s?.position ?? Duration.zero),
            textAlign: TextAlign.right,
            style: const TextStyle(
              fontSize: 14,
              fontFamily: 'bitroad',
              letterSpacing: 1,
              color: Colors.white38,
            ),
          ),
        ),
      ],
    );
  }
}

/// A horizontal cluster of tape-deck-style transport buttons.
class _TransportBar extends StatelessWidget {
  final bool isPlaying;
  final VoidCallback onRec;
  final VoidCallback onRew;
  final VoidCallback onPlay;
  final VoidCallback onFfwd;
  final VoidCallback onStop;
  final VoidCallback onPause;

  const _TransportBar({
    required this.isPlaying,
    required this.onRec,
    required this.onRew,
    required this.onPlay,
    required this.onFfwd,
    required this.onStop,
    required this.onPause,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        _TapeButton(
          icon: Icons.folder_open,
          tooltip: 'Load track',
          onTap: onRec,
        ),
        _TapeButton(icon: Icons.fast_rewind, onTap: onRew),
        _TapeButton(
          icon: isPlaying ? Icons.pause : Icons.play_arrow,
          lit: isPlaying,
          width: 104, // Roughly 2 spaces + padding
          onTap: isPlaying ? onPause : onPlay,
        ),
        _TapeButton(icon: Icons.fast_forward, onTap: onFfwd),
        _TapeButton(icon: Icons.stop, onTap: onStop),
      ],
    );
  }
}

class _TapeButton extends StatelessWidget {
  final IconData icon;
  final VoidCallback onTap;
  final bool lit;
  final String? tooltip;
  final double? width;

  const _TapeButton({
    required this.icon,
    required this.onTap,
    this.lit = false,
    this.tooltip,
    this.width,
  });

  @override
  Widget build(BuildContext context) {
    final glyph = lit ? const Color(0xFFE0344B) : Colors.white38;
    final btn = Padding(
      padding: const EdgeInsets.symmetric(horizontal: 2),
      child: InkWell(
        borderRadius: BorderRadius.circular(4),
        onTap: onTap,
        hoverColor: Colors.white12,
        splashColor: Colors.transparent,
        highlightColor: Colors.transparent,
        child: Container(
          width: width,
          padding: const EdgeInsets.all(6),
          child: Icon(icon, size: 28, color: glyph),
        ),
      ),
    );
    return tooltip == null ? btn : Tooltip(message: tooltip!, child: btn);
  }
}

/// Loop controls: set in-point (A), out-point (B, activates), and exit. A and B
/// light up while a loop is active.
class _LoopBar extends StatelessWidget {
  final DeckState? state;
  final VoidCallback onRecall;
  final VoidCallback onA;
  final VoidCallback onB;
  final VoidCallback onExit;

  const _LoopBar({
    required this.state,
    required this.onRecall,
    required this.onA,
    required this.onB,
    required this.onExit,
  });

  @override
  Widget build(BuildContext context) {
    final active = state?.loopActive ?? false;
    final hasCue = state?.hasCue ?? false;
    final hasRecall = state?.hasRecall ?? false;
    final blink = (DateTime.now().millisecondsSinceEpoch ~/ 500) % 2 == 0;

    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        _LoopButton(
          icon: Icons.replay,
          enabled: hasCue || hasRecall,
          lit: hasRecall && !active && blink,
          tooltip: 'Reloop / Recall',
          onTap: onRecall,
        ),
        _LoopButton(
          text: 'A',
          lit: active || (hasCue && blink),
          tooltip: 'Set loop in-point',
          onTap: onA,
        ),
        _LoopButton(
          text: 'B',
          lit: active,
          tooltip: 'Set loop out-point',
          onTap: onB,
        ),
        _LoopButton(
          icon: Icons.close,
          enabled: hasCue,
          lit: active && blink,
          tooltip: 'Clear Cue/Loop',
          onTap: onExit,
        ),
      ],
    );
  }
}

class _LoopButton extends StatelessWidget {
  final String? text;
  final IconData? icon;
  final VoidCallback onTap;
  final bool lit;
  final bool enabled;
  final String tooltip;

  const _LoopButton({
    required this.onTap,
    required this.tooltip,
    this.text,
    this.icon,
    this.lit = false,
    this.enabled = true,
  });

  @override
  Widget build(BuildContext context) {
    final fg = !enabled
        ? Colors.white12
        : lit
            ? const Color(0xFFE0344B)
            : Colors.white38;
    return Tooltip(
      message: tooltip,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 2),
        child: InkWell(
          borderRadius: BorderRadius.circular(4),
          onTap: enabled ? onTap : null,
          hoverColor: enabled ? Colors.white12 : Colors.transparent,
          splashColor: Colors.transparent,
          highlightColor: Colors.transparent,
          child: Padding(
            padding: const EdgeInsets.all(6),
            child: text != null
                ? Text(
                    text!,
                    style: TextStyle(
                      fontSize: 24,
                      height: 1.0,
                      fontWeight: FontWeight.w600,
                      color: fg,
                    ),
                  )
                : Icon(icon, size: 28, color: fg),
          ),
        ),
      ),
    );
  }
}

/// Decorative signal-routing diagram down the right edge: each deck taps into a
/// vertical bus that flows to a master output node.
class _RoutingDiagram extends StatelessWidget {
  final int deckCount;
  final List<DeckState?> states;
  const _RoutingDiagram({required this.deckCount, required this.states});

  @override
  Widget build(BuildContext context) {
    final active = [
      for (var i = 0; i < deckCount; i++)
        (i < states.length ? states[i]?.isPlaying : false) ?? false,
    ];
    return CustomPaint(painter: _RoutingPainter(active));
  }
}

class _RoutingPainter extends CustomPainter {
  final List<bool> active;
  _RoutingPainter(this.active);

  static const _wire = Color(0x33FFFFFF);
  static const _hot = Color(0xFFE0344B);

  @override
  void paint(Canvas canvas, Size size) {
    final n = active.isEmpty ? 1 : active.length;
    final busX = size.width * 0.42;
    final topY = size.height * (0.5 / n);
    final outY = size.height - 28;

    final wire = Paint()
      ..color = _wire
      ..strokeWidth = 1.5
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;
    final hot = Paint()
      ..color = _hot
      ..strokeWidth = 2
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;

    // Vertical bus.
    canvas.drawLine(Offset(busX, topY), Offset(busX, outY), wire);

    // Per-deck tap from the left edge into the bus.
    for (var i = 0; i < n; i++) {
      final y = size.height * ((i + 0.5) / n);
      final p = active[i] ? hot : wire;
      final path = Path()
        ..moveTo(0, y)
        ..lineTo(busX - 8, y)
        ..quadraticBezierTo(busX, y, busX, y + (y < outY ? 8 : -8));
      canvas.drawPath(path, p);
      canvas.drawCircle(Offset(0, y), 3, Paint()..color = active[i] ? _hot : _wire);
    }

    // Master output node + label.
    final anyHot = active.any((a) => a);
    canvas.drawCircle(
      Offset(busX, outY),
      4.5,
      Paint()..color = anyHot ? _hot : _wire,
    );
    final tp = TextPainter(
      text: TextSpan(
        text: 'OUT',
        style: TextStyle(
          color: anyHot ? _hot : Colors.white38,
          fontSize: 10,
          letterSpacing: 2,
          fontWeight: FontWeight.w600,
        ),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(canvas, Offset(busX - tp.width / 2, outY + 8));
  }

  @override
  bool shouldRepaint(_RoutingPainter old) => !_listEq(old.active, active);

  static bool _listEq(List<bool> a, List<bool> b) {
    if (a.length != b.length) return false;
    for (var i = 0; i < a.length; i++) {
      if (a[i] != b[i]) return false;
    }
    return true;
  }
}
