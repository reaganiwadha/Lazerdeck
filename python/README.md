# lazerdeck-py

Python client library for the **Lazerdeck** audio engine control plane.

This library provides a clean, Pythonic REPL and scripting interface to interact with a running Lazerdeck engine instance. It translates Python method calls into well-formed JSON RPC actions and manages communication over HTTP.

## Installation

To install in editable/development mode:

```bash
cd python
pip install -e .[dev]
```

## Quick Start

Ensure your Lazerdeck audio engine is running with its Control Server enabled (defaulting to port `8203`).

```python
from lazerdeck import Engine

# Initialize client (defaults to http://127.0.0.1:8203)
engine = Engine()

# Check engine liveness
if not engine.health():
    print("Lazerdeck engine is offline!")
    exit(1)

# Fetch current deck states
states = engine.get_state()
for state in states:
    print(f"Deck {state.deck}: {state.filepath} - {'Playing' if state.is_playing else 'Paused'}")

# Load a track on deck 1 and start playing
deck1 = engine.d1
deck1.load("C:/music/my_track.wav")
deck1.play()

# Set deck 1 volume and EQ mid
deck1.volume(0.8)
deck1.eq_mid(0.4)

# Set sync intent on deck 2 and align it
deck2 = engine.d2
deck2.load("C:/music/another_track.mp3")
deck2.sync(True)
deck2.play()

# Align deck 1 beat 32 with deck 2 beat 64
deck1.align(32.0, deck2, 64.0)
```

## Batching Commands

You can batch several commands into one atomic HTTP POST request using the `batch()` context manager. This ensures they arrive together and execute on the same engine thread tick.

```python
with engine.batch():
    engine.d1.volume(0.0)
    engine.d2.volume(1.0)
    engine.d2.play()
```

## Scheduled Commands (`on_beat`)

Almost all deck methods accept an optional `on_beat` parameter. If provided, the command is deferred and executed automatically when that deck's playhead reaches the specified 1-based beat.

```python
# Cut deck 1 lows at beat 64, restore them at beat 96
engine.d1.eq_low(0.0, on_beat=64)
engine.d1.eq_low(0.5, on_beat=96)
```

## Client-Side Validation

The library performs client-side validation for:
- Out of range EQ/volume settings (must be between `0.0` and `1.0`).
- Speed multiplier ranges (must be greater than `0.0`).
- Beat and scheduling values (must be `>= 1.0`).
- Invalid argument structures.

If validation fails, a `ValidationError` is raised immediately without hitting the network.
