import 'dart:async';
import 'dart:ui';
import 'package:flutter/material.dart';
import 'package:desktop_multi_window/desktop_multi_window.dart';
import 'package:window_manager/window_manager.dart';

import 'ffi/engine.dart';
import 'ui/decks_view.dart';
import 'ui/script_editor.dart';
import 'ui/window_title.dart';

Future<void> main(List<String> args) async {
  WidgetsFlutterBinding.ensureInitialized();

  // Every window (main + sub-windows) runs this same entrypoint. We ask the
  // plugin which window we are; the editor sub-window is created with the
  // 'editor' argument (see openScriptEditorWindow). Guarded so any multi-window
  // failure falls back to the main app rather than blocking startup.
  String windowArgs = '';
  try {
    final wc = await WindowController.fromCurrentEngine();
    windowArgs = wc.arguments;
  } catch (_) {
    windowArgs = '';
  }

  if (windowArgs.contains('editor')) {
    // Editor sub-window. Render immediately — do NOT await window_manager here:
    // in a sub-window engine its channel can hang, which would block the first
    // frame and leave the window blank. Sizing/title is best-effort below.
    // This window does NOT init the engine (the main window owns it); it pushes
    // commands to the shared process-global engine via its own DLL handle.
    unawaited(_styleEditorWindow());
    runApp(const EditorWindowApp());
    return;
  }

  final engine = LazerdeckEngine.load();
  final ok = engine.init(numDecks: 2);
  runApp(LazerdeckApp(engine: engine, initOk: ok));
}

/// Best-effort sizing/title for the editor sub-window. Never awaited from the
/// startup path so it can't block the first frame; all failures are swallowed.
Future<void> _styleEditorWindow() async {
  try {
    await windowManager.ensureInitialized();
    await windowManager.setTitle('LazerScript');
    await windowManager.setSize(const Size(760, 560));
    await windowManager.center();
    await windowManager.show();
  } catch (_) {}
}

class LazerdeckApp extends StatefulWidget {
  final LazerdeckEngine engine;
  final bool initOk;
  const LazerdeckApp({super.key, required this.engine, required this.initOk});

  @override
  State<LazerdeckApp> createState() => _LazerdeckAppState();
}

class _LazerdeckAppState extends State<LazerdeckApp> with WidgetsBindingObserver {
  late final WindowTitleMonitor _titleMonitor;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _titleMonitor = WindowTitleMonitor(widget.engine);
    if (widget.initOk) _titleMonitor.start();
  }

  @override
  void dispose() {
    _titleMonitor.stop();
    WidgetsBinding.instance.removeObserver(this);
    widget.engine.dispose();
    super.dispose();
  }

  @override
  Future<AppExitResponse> didRequestAppExit() async {
    widget.engine.dispose();
    return AppExitResponse.exit;
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Lazerdeck',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        brightness: Brightness.dark,
        useMaterial3: true,
        scaffoldBackgroundColor: Colors.black,
        colorScheme: const ColorScheme.dark(
          primary: Color(0xFFE0344B), // minimal accent: tape-deck REC red
          secondary: Color(0xFFE0344B),
          surface: Color(0xFF0A0A0A),
          surfaceContainerHighest: Color(0xFF141414),
        ),
      ),
      home: widget.initOk ? DecksView(engine: widget.engine) : const _EngineError(),
    );
  }
}

class _EngineError extends StatelessWidget {
  const _EngineError();
  @override
  Widget build(BuildContext context) {
    return const Scaffold(
      body: Center(
        child: Text(
          'Failed to initialize the Lazerdeck engine.\n'
          'Is lazerdeck_engine.dll next to the executable?',
          textAlign: TextAlign.center,
        ),
      ),
    );
  }
}
