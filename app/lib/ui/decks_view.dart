import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';

import '../ffi/engine.dart';

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
    // ~25 Hz UI refresh of deck state / timecode.
    _poll = Timer.periodic(const Duration(milliseconds: 40), (_) => _refresh());
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
    return Scaffold(
      appBar: AppBar(
        title: const Text('LAZERDECK',
            style: TextStyle(letterSpacing: 4, fontWeight: FontWeight.bold)),
        backgroundColor: Colors.transparent,
      ),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Row(
          children: [
            for (var i = 0; i < widget.engine.deckCount; i++)
              Expanded(
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 8),
                  child: _DeckCard(
                    index: i,
                    state: i < _states.length ? _states[i] : null,
                    onOpen: () => _openFile(i),
                    onPlay: () => widget.engine.play(i),
                    onPause: () => widget.engine.pause(i),
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

class _DeckCard extends StatelessWidget {
  final int index;
  final DeckState? state;
  final VoidCallback onOpen;
  final VoidCallback onPlay;
  final VoidCallback onPause;

  const _DeckCard({
    required this.index,
    required this.state,
    required this.onOpen,
    required this.onPlay,
    required this.onPause,
  });

  static String _basename(String p) {
    final i = p.lastIndexOf(RegExp(r'[\\/]'));
    return i >= 0 ? p.substring(i + 1) : p;
  }

  static String _timecode(Duration d) {
    final m = d.inMinutes.remainder(60).toString().padLeft(2, '0');
    final s = d.inSeconds.remainder(60).toString().padLeft(2, '0');
    final cs = (d.inMilliseconds.remainder(1000) ~/ 10).toString().padLeft(2, '0');
    return '$m:$s.$cs';
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final s = state;
    final label = String.fromCharCode('A'.codeUnitAt(0) + index);
    final track = (s != null && s.hasTrack) ? _basename(s.filepath) : 'No track loaded';
    final playing = s?.isPlaying ?? false;

    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: cs.surface,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(
          color: playing ? cs.primary : Colors.white12,
          width: playing ? 2 : 1,
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              Text('DECK $label',
                  style: TextStyle(
                      color: cs.primary,
                      fontSize: 20,
                      fontWeight: FontWeight.bold,
                      letterSpacing: 2)),
              const Spacer(),
              if (s?.isLoading ?? false) _chip('LOADING', Colors.orange),
              if (s?.isAnalyzing ?? false) _chip('ANALYZING', Colors.purpleAccent),
            ],
          ),
          const SizedBox(height: 16),
          Text(track,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(fontSize: 14, color: Colors.white70)),
          const SizedBox(height: 20),
          Center(
            child: Text(
              _timecode(s?.position ?? Duration.zero),
              style: TextStyle(
                fontSize: 48,
                fontFeatures: const [],
                fontFamily: 'monospace',
                color: playing ? cs.primary : Colors.white,
              ),
            ),
          ),
          const SizedBox(height: 8),
          Center(
            child: Text(
              s != null && s.bpm > 0
                  ? '${s.bpm.toStringAsFixed(1)} BPM   x${s.speed.toStringAsFixed(2)}'
                  : '-- BPM',
              style: const TextStyle(color: Colors.white38),
            ),
          ),
          const Spacer(),
          Row(
            children: [
              Expanded(
                child: OutlinedButton.icon(
                  onPressed: onOpen,
                  icon: const Icon(Icons.folder_open),
                  label: const Text('Open'),
                ),
              ),
              const SizedBox(width: 8),
              IconButton.filled(
                onPressed: onPlay,
                icon: const Icon(Icons.play_arrow),
                tooltip: 'Play',
              ),
              const SizedBox(width: 4),
              IconButton.filledTonal(
                onPressed: onPause,
                icon: const Icon(Icons.pause),
                tooltip: 'Pause',
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _chip(String text, Color color) => Container(
        margin: const EdgeInsets.only(left: 6),
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
        decoration: BoxDecoration(
          color: color.withValues(alpha: 0.2),
          borderRadius: BorderRadius.circular(8),
        ),
        child: Text(text,
            style: TextStyle(color: color, fontSize: 10, fontWeight: FontWeight.bold)),
      );
}
