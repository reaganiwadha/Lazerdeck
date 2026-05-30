// Audio settings panel: shows the engine's live output configuration and lets
// the user switch output device. Switching reopens the engine's audio stream
// on the engine thread (tracks stay loaded); we then poll the config until it
// reflects the new device.
import 'dart:async';

import 'package:flutter/material.dart';

import '../ffi/engine.dart';

Future<void> showAudioSettings(BuildContext context, LazerdeckEngine engine) {
  return showDialog<void>(
    context: context,
    builder: (_) => Dialog(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 520, maxHeight: 600),
        child: _AudioSettings(engine: engine),
      ),
    ),
  );
}

class _AudioSettings extends StatefulWidget {
  final LazerdeckEngine engine;
  const _AudioSettings({required this.engine});

  @override
  State<_AudioSettings> createState() => _AudioSettingsState();
}

class _AudioSettingsState extends State<_AudioSettings> {
  List<AudioDevice> _devices = const [];
  AudioConfig? _config;
  int? _switchingTo; // device index a switch is in flight to
  int? _switchingRate; // sample rate a switch is in flight to
  Timer? _confirmPoll;

  ControlServerStatus _control =
      const ControlServerStatus(running: false, port: 0);
  final TextEditingController _portCtrl = TextEditingController();
  String? _controlError; // last bind failure message, cleared on success

  @override
  void initState() {
    super.initState();
    _reload();
    _control = widget.engine.controlServerStatus();
    _portCtrl.text =
        (_control.port == 0 ? LazerdeckEngine.defaultControlPort : _control.port)
            .toString();
  }

  void _reload() {
    setState(() {
      _devices = widget.engine.audioDevices();
      _config = widget.engine.audioConfig();
      _control = widget.engine.controlServerStatus();
    });
  }

  void _startControl() {
    final port = int.tryParse(_portCtrl.text.trim());
    if (port == null || port < 1 || port > 65535) {
      setState(() => _controlError = 'Enter a port between 1 and 65535');
      return;
    }
    final ok = widget.engine.startControlServer(port);
    setState(() {
      _control = widget.engine.controlServerStatus();
      _controlError = ok ? null : 'Port $port is unavailable — try another';
    });
  }

  void _stopControl() {
    widget.engine.stopControlServer();
    setState(() {
      _control = widget.engine.controlServerStatus();
      _controlError = null;
    });
  }

  void _select(AudioDevice device) {
    if (device.index == _config?.deviceIndex) return;
    widget.engine.switchAudioDevice(device.index);
    setState(() => _switchingTo = device.index);

    // The reopen is async on the engine thread; poll until the config catches
    // up (or give up after ~2s).
    _confirmPoll?.cancel();
    var ticks = 0;
    _confirmPoll = Timer.periodic(const Duration(milliseconds: 120), (t) {
      ticks++;
      final cfg = widget.engine.audioConfig();
      final done = cfg?.deviceIndex == device.index;
      if (done || ticks > 16) {
        t.cancel();
        if (mounted) {
          setState(() {
            _switchingTo = null;
            _config = cfg;
            _devices = widget.engine.audioDevices(); // refresh isCurrent flags
          });
        }
      } else if (mounted) {
        setState(() => _config = cfg);
      }
    });
  }

  void _selectRate(int rate) {
    if (rate == _config?.sampleRate) return;
    final oldRate = _config?.sampleRate;
    widget.engine.setSampleRate(rate);
    setState(() => _switchingRate = rate);

    // The reopen + track re-decode runs on the engine thread; the config's
    // sample rate flips as soon as the stream reopens. Poll until it changes
    // (the device may grant a nearby rate instead) or we give up after ~3s.
    _confirmPoll?.cancel();
    var ticks = 0;
    _confirmPoll = Timer.periodic(const Duration(milliseconds: 150), (t) {
      ticks++;
      final cfg = widget.engine.audioConfig();
      final done = cfg?.sampleRate == rate || cfg?.sampleRate != oldRate;
      if (done || ticks > 20) {
        t.cancel();
        if (mounted) {
          setState(() {
            _switchingRate = null;
            _config = cfg;
            _devices = widget.engine.audioDevices();
          });
        }
      } else if (mounted) {
        setState(() => _config = cfg);
      }
    });
  }

  @override
  void dispose() {
    _confirmPoll?.cancel();
    _portCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final cfg = _config;

    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(20, 20, 12, 8),
          child: Row(
            children: [
              Icon(Icons.tune, color: cs.primary),
              const SizedBox(width: 10),
              const Text('Audio Settings',
                  style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
              const Spacer(),
              IconButton(
                tooltip: 'Refresh',
                onPressed: _reload,
                icon: const Icon(Icons.refresh),
              ),
              IconButton(
                tooltip: 'Close',
                onPressed: () => Navigator.of(context).pop(),
                icon: const Icon(Icons.close),
              ),
            ],
          ),
        ),
        if (cfg != null) _ConfigSummary(config: cfg),
        const Divider(height: 1),
        Padding(
          padding: const EdgeInsets.fromLTRB(20, 14, 20, 6),
          child: Text('SAMPLE RATE',
              style: TextStyle(
                  color: cs.primary,
                  fontSize: 12,
                  letterSpacing: 1.5,
                  fontWeight: FontWeight.bold)),
        ),
        Padding(
          padding: const EdgeInsets.fromLTRB(20, 0, 20, 4),
          child: Row(
            children: [
              for (final rate in LazerdeckEngine.supportedSampleRates)
                Padding(
                  padding: const EdgeInsets.only(right: 8),
                  child: ChoiceChip(
                    label: Text('${(rate / 1000).toStringAsFixed(1)} kHz'),
                    selected: cfg?.sampleRate == rate,
                    onSelected: _switchingRate != null
                        ? null
                        : (_) => _selectRate(rate),
                  ),
                ),
              if (_switchingRate != null)
                const SizedBox(
                  width: 18,
                  height: 18,
                  child: CircularProgressIndicator(strokeWidth: 2),
                ),
            ],
          ),
        ),
        const Padding(
          padding: EdgeInsets.fromLTRB(20, 0, 20, 12),
          child: Text('Changing the rate reloads playing tracks.',
              style: TextStyle(fontSize: 11, color: Colors.white38)),
        ),
        const Divider(height: 1),
        _controlSection(cs),
        const Divider(height: 1),
        Padding(
          padding: const EdgeInsets.fromLTRB(20, 14, 20, 6),
          child: Text('OUTPUT DEVICE',
              style: TextStyle(
                  color: cs.primary,
                  fontSize: 12,
                  letterSpacing: 1.5,
                  fontWeight: FontWeight.bold)),
        ),
        Flexible(
          child: _devices.isEmpty
              ? const Padding(
                  padding: EdgeInsets.all(20),
                  child: Text('No output devices found.',
                      style: TextStyle(color: Colors.white54)),
                )
              : ListView.builder(
                  shrinkWrap: true,
                  itemCount: _devices.length,
                  itemBuilder: (_, i) {
                    final d = _devices[i];
                    final isCurrent = d.index == cfg?.deviceIndex;
                    final isSwitching = d.index == _switchingTo;
                    return ListTile(
                      leading: isSwitching
                          ? const SizedBox(
                              width: 22,
                              height: 22,
                              child: CircularProgressIndicator(strokeWidth: 2),
                            )
                          : Icon(
                              isCurrent
                                  ? Icons.radio_button_checked
                                  : Icons.radio_button_unchecked,
                              color: isCurrent ? cs.primary : Colors.white38,
                            ),
                      title: Text(d.name, maxLines: 1, overflow: TextOverflow.ellipsis),
                      subtitle: Text(
                        '${d.hostApi} · ${d.maxOutputChannels} ch · '
                        '${d.defaultSampleRate} Hz${d.isDefault ? ' · default' : ''}',
                        style: const TextStyle(fontSize: 12),
                      ),
                      onTap: _switchingTo == null ? () => _select(d) : null,
                    );
                  },
                ),
        ),
        const SizedBox(height: 8),
      ],
    );
  }

  Widget _controlSection(ColorScheme cs) {
    final running = _control.running;
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 14, 20, 12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Text('CONTROL SERVER',
                  style: TextStyle(
                      color: cs.primary,
                      fontSize: 12,
                      letterSpacing: 1.5,
                      fontWeight: FontWeight.bold)),
              const Spacer(),
              Icon(running ? Icons.circle : Icons.circle_outlined,
                  size: 12,
                  color: running ? Colors.greenAccent : Colors.white38),
              const SizedBox(width: 6),
              Text(
                running ? 'Running on :${_control.port}' : 'Stopped',
                style: TextStyle(
                    fontSize: 12,
                    color: running ? Colors.greenAccent : Colors.white54),
              ),
            ],
          ),
          const SizedBox(height: 4),
          const Text('HTTP/JSON endpoint at POST /action for external control.',
              style: TextStyle(fontSize: 11, color: Colors.white38)),
          const SizedBox(height: 10),
          Row(
            children: [
              SizedBox(
                width: 110,
                child: TextField(
                  controller: _portCtrl,
                  keyboardType: TextInputType.number,
                  decoration: const InputDecoration(
                    labelText: 'Port',
                    isDense: true,
                    border: OutlineInputBorder(),
                  ),
                  onSubmitted: (_) => _startControl(),
                ),
              ),
              const SizedBox(width: 12),
              FilledButton.icon(
                onPressed: _startControl,
                icon: const Icon(Icons.play_arrow, size: 18),
                label: Text(running ? 'Restart' : 'Start'),
              ),
              const SizedBox(width: 8),
              OutlinedButton.icon(
                onPressed: running ? _stopControl : null,
                icon: const Icon(Icons.stop, size: 18),
                label: const Text('Stop'),
              ),
            ],
          ),
          if (_controlError != null) ...[
            const SizedBox(height: 8),
            Text(_controlError!,
                style: TextStyle(fontSize: 12, color: cs.error)),
          ],
        ],
      ),
    );
  }
}

class _ConfigSummary extends StatelessWidget {
  final AudioConfig config;
  const _ConfigSummary({required this.config});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 4, 20, 14),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            config.deviceName.isEmpty ? 'No device' : config.deviceName,
            style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w600),
          ),
          const SizedBox(height: 8),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              _stat('API', config.hostApi),
              _stat('Sample rate', '${config.sampleRate} Hz'),
              _stat('Buffer', '${config.bufferFrames} frames'),
              _stat('Bit depth', '${config.bitDepth}-bit'),
              _stat('Latency', '~${config.latencyMs} ms'),
            ],
          ),
        ],
      ),
    );
  }

  Widget _stat(String label, String value) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
        decoration: BoxDecoration(
          color: Colors.white10,
          borderRadius: BorderRadius.circular(8),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(label.toUpperCase(),
                style: const TextStyle(
                    fontSize: 9,
                    color: Colors.white38,
                    letterSpacing: 1)),
            const SizedBox(height: 2),
            Text(value,
                style: const TextStyle(
                    fontSize: 13, fontWeight: FontWeight.w600)),
          ],
        ),
      );
}
