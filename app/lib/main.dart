import 'dart:ui';
import 'package:flutter/material.dart';

import 'ffi/engine.dart';
import 'ui/decks_view.dart';
import 'ui/window_title.dart';

void main() {
  final engine = LazerdeckEngine.load();
  final ok = engine.init(numDecks: 2);
  runApp(LazerdeckApp(engine: engine, initOk: ok));
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
      theme: ThemeData.dark(useMaterial3: true).copyWith(
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
