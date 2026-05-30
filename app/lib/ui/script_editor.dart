// LazerScript editor — runs in its own OS window (via desktop_multi_window).
//
// The window is a separate Flutter engine/isolate but lives in the SAME process
// as the main app, so it loads the engine DLL and pushes commands straight to
// the shared process-global engine (its command queue is thread-safe). No
// cross-window IPC is needed.
//
// Editing uses flutter_code_editor for syntax highlighting + autocompletion,
// with a custom highlight mode and command vocabulary for LazerScript.
import 'package:desktop_multi_window/desktop_multi_window.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_code_editor/flutter_code_editor.dart';
import 'package:flutter_highlight/themes/monokai.dart';
import 'package:highlight/highlight_core.dart' show Mode;

import '../ffi/engine.dart';

const _accent = Color(0xFFE0344B);

/// LazerScript command vocabulary — drives both highlighting and autocomplete.
const _keywords = <String>[
  // grammar
  'when', 'hit', 'do', 'onBeat', 'clear',
  // transport / deck
  'play', 'pause', 'stop', 'load', 'seek', 'playjump',
  'speed', 'speed_up', 'speed_down', 'speed_reset',
  // mixer / params
  'volume', 'vol', 'low', 'mid', 'high', 'eq_low', 'eq_mid', 'eq_high',
  // tempo / grid
  'bpm', 'offset', 'nudge_offset', 'reanalyze', 'metronome', 'sync',
  // loops / cues
  'loop', 'loop_in', 'loop_out', 'loop_exit', 'loop_clear', 'reloop',
  'cue', 'goto_cue',
];

/// Custom highlight.js-style mode for LazerScript.
final Mode _lazerMode = Mode(
  refs: <String, Mode>{},
  keywords: {'keyword': _keywords.join(' ')},
  contains: [
    Mode(className: 'comment', begin: '#', end: r'$'),
    Mode(className: 'comment', begin: '//', end: r'$'),
    Mode(className: 'variable', begin: r'\$d\d+'), // deck refs: $d1, $d2…
    Mode(className: 'number', begin: r'-?\b\d+(\.\d+)?\b'),
  ],
);

/// Opens the LazerScript editor window, focusing an already-open one instead of
/// spawning duplicates. Called from the main app window.
Future<void> openScriptEditorWindow() async {
  // Reuse an existing editor window if one is already open.
  for (final c in await WindowController.getAll()) {
    if (c.arguments.contains('editor')) {
      await c.show();
      return;
    }
  }
  final c = await WindowController.create(
    const WindowConfiguration(arguments: 'editor', hiddenAtLaunch: true),
  );
  await c.show();
}

/// Root widget for the editor sub-window (its own MaterialApp + theme).
class EditorWindowApp extends StatelessWidget {
  const EditorWindowApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'LazerScript',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true).copyWith(
        scaffoldBackgroundColor: const Color(0xFF101012),
        colorScheme: const ColorScheme.dark(
          primary: _accent,
          secondary: _accent,
          surface: Color(0xFF0A0A0A),
        ),
      ),
      home: const ScriptEditorPage(),
    );
  }
}

class ScriptEditorPage extends StatefulWidget {
  const ScriptEditorPage({super.key});

  @override
  State<ScriptEditorPage> createState() => _ScriptEditorPageState();
}

class _ScriptEditorPageState extends State<ScriptEditorPage> {
  late final CodeController _code;
  late final LazerdeckEngine _engine; // DLL handle; shares process-global engine
  String _status = '';

  @override
  void initState() {
    super.initState();
    _code = CodeController(
      language: _lazerMode,
      text: '# LazerScript — one command per line. Ctrl/Cmd+Enter to run.\n'
          '# Beats are 1-based and match the waveform grid (beat 1 = start).\n'
          '\$d1 low 0.3\n'
          '\$d1 low (onBeat 16 0.5 32 0.0)\n'
          '\$d2 play when \$d1 64\n'
          '\$d2 playjump 16 when \$d1 64\n',
    );
    // Autocomplete from the command vocabulary plus the two deck refs.
    _code.autocompleter.setCustomWords([..._keywords, r'$d1', r'$d2']);
    // No engine.init() here — the main window owns the engine instance; this
    // handle only pushes commands onto the shared queue.
    _engine = LazerdeckEngine.load();
  }

  @override
  void dispose() {
    _code.dispose();
    super.dispose();
  }

  void _run() {
    var sent = 0;
    for (final raw in _code.fullText.split('\n')) {
      final line = raw.trim();
      if (line.isEmpty || line.startsWith('#') || line.startsWith('//')) {
        continue;
      }
      _engine.pushCommand(line);
      sent++;
    }
    setState(() =>
        _status = sent == 0 ? 'Nothing to run' : 'Ran $sent line${sent == 1 ? '' : 's'}');
  }

  @override
  Widget build(BuildContext context) {
    return CallbackShortcuts(
      bindings: {
        const SingleActivator(LogicalKeyboardKey.enter, control: true): _run,
        const SingleActivator(LogicalKeyboardKey.enter, meta: true): _run,
      },
      child: Scaffold(
        body: Column(
          children: [
            Expanded(
              child: CodeTheme(
                data: CodeThemeData(styles: monokaiTheme),
                child: SingleChildScrollView(
                  child: CodeField(
                    controller: _code,
                    expands: false,
                    wrap: false,
                    textStyle: const TextStyle(
                      fontFamily: 'JetBrainsMono',
                      fontSize: 13,
                      height: 1.4,
                    ),
                  ),
                ),
              ),
            ),
            _bottomBar(),
          ],
        ),
      ),
    );
  }

  Widget _bottomBar() {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: const BoxDecoration(
        color: Color(0xFF161618),
        border: Border(top: BorderSide(color: Colors.white12)),
      ),
      child: Row(
        children: [
          const Icon(Icons.terminal, size: 16, color: _accent),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              _status.isEmpty
                  ? '“\$dT play when \$dS b” / “\$dT playjump tb when \$dS b” fire when deck S hits beat b'
                  : _status,
              style: const TextStyle(fontSize: 11, color: Colors.white38),
            ),
          ),
          FilledButton.icon(
            style: FilledButton.styleFrom(backgroundColor: _accent),
            onPressed: _run,
            icon: const Icon(Icons.play_arrow, size: 18),
            label: const Text('Run  (Ctrl+Enter)'),
          ),
        ],
      ),
    );
  }
}
