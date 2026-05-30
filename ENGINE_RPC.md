# Lazerdeck Engine — JSON RPC

This document describes the HTTP/JSON control plane of the Lazerdeck audio engine
and how to build a Python REPL library on top of it. It is written for an agent
building that library: it covers the wire protocol, every verb, the semantics you
must respect, and a map into the relevant C++ source.

---

## 1. Transport

- The engine runs a small HTTP server (cpp-httplib) on **`127.0.0.1:8203`** by
  default. It is localhost-only — there is no auth and it must never be exposed.
- A busy port at engine startup is **non-fatal**: the server simply stays
  stopped. The port can be changed and the server started/stopped from the
  desktop app's *Audio Settings → Control Server* panel (this is host-side; a
  network client cannot change the port — it can only talk to whatever port the
  server is currently on).
- Two endpoints:
  - `POST /action` — the command channel (JSON body, described below).
  - `GET /health` — liveness probe, returns `{"ok":true}`. Use this to detect
    whether the engine is up and on which port.

There is **no** RPC method to read deck state over HTTP yet — the control plane
is write-only (fire commands). Deck state is currently only available in-process
via the C FFI (`lazerdeck_get_deck_state`). If the Python library needs feedback,
note this gap (see §8); a future `GET /state` endpoint would be the place to add
it (`ControlServer::start` in `engine/src/ControlServer.cpp`).

---

## 2. Request / response format

### Request body

```json
{
  "actions": [
    { "action": "load", "deck": "d1", "path": "C:/music/a.wav" },
    { "action": "play", "deck": "d1" },
    { "action": "pause", "deck": "d2", "onBeat": 32 }
  ]
}
```

- The top-level object has a single key, **`actions`**: a flat array of action
  objects. It is a flat list (not keyed by deck) because some verbs — `align` —
  act across two decks. Each action names its own deck.
- Actions in one POST are enqueued **in order** and executed in order on the
  engine thread. Batching several actions in one request is the way to get them
  applied together (e.g. `load` + `play`).

### Response

- **Success:** HTTP 200, body `{"ok":true,"error":""}`. This means the request
  *parsed and was accepted* — see §3 for why that is not the same as "every
  action succeeded".
- **Failure (parse/validation):** HTTP 400, body `{"ok":false,"error":"<why>"}`.
  Returned when the JSON is malformed or the `actions` array is empty.

---

## 3. Execution model & conventions (read this)

These are the rules a client author must internalize:

1. **Async, fire-and-forget.** The HTTP thread only parses + validates. It then
   queues each action onto the engine thread and returns `{"ok":true}`
   immediately. The actual deck mutation happens a few milliseconds later.
2. **Only parse/validation errors are reported.** Unknown verb names, bad deck
   indices, a `load` with no `path`, an `align` against a deck with no tempo —
   none of these produce an HTTP error. They are logged engine-side and become
   no-ops. A `200 {"ok":true}` means "well-formed and accepted", *not* "it
   worked". The Python library should validate verb names / required fields /
   deck ranges **client-side** if it wants to surface mistakes to the user.
3. **Deck names are 1-based:** `"d1"`, `"d2"`, … The parser also accepts `"D1"`
   and a bare `"1"`. Internally these map to 0-based indices.
   (`parseDeckName` in `engine/src/Engine.cpp`.)
4. **Beats are 1-based.** Beat `1` is the grid origin (first downbeat). This
   matches the UI. `onBeat`, `beat` (playjump), and the `align` beats all use
   this convention. Fractional beats are allowed.
5. **EQ / volume values are `0.0 … 1.0`, with `0.5` = unity.** Not dB.
6. **Speed is a multiplier** (`1.0` = normal, `2.0` = double-time).
7. **Paths** are passed to the OS file loader verbatim — give absolute paths.
   Forward slashes work on Windows. Supported formats include WAV and MP3.

---

## 4. Verb reference

Every action object has an `action` string plus the fields below. Unlisted
fields are ignored. `deck` is required for all single-deck verbs.

| `action`        | Required fields            | Optional       | Effect |
|-----------------|----------------------------|----------------|--------|
| `load`          | `deck`, `path`             | `onBeat`       | Decode + analyze a file onto the deck (replaces any current track; BPM/beatgrid auto-detected). |
| `eject`         | `deck`                     | `onBeat`       | Unload the track; reset tempo/loop/sync/speed and drop the deck's automation + scheduled triggers. |
| `play`          | `deck`                     | `onBeat`       | Start playback. |
| `pause`         | `deck`                     | `onBeat`       | Pause (keeps position). |
| `stop`          | `deck`                     | `onBeat`       | Pause and seek to the start. |
| `seek`          | `deck`, `value` (seconds)  | `onBeat`       | Seek by an **offset in seconds** (relative; can be negative). |
| `speed`         | `deck`, `speed` (mult)     | `onBeat`       | Set playback speed multiplier. |
| `speed_reset`   | `deck`                     | `onBeat`       | Reset speed to `1.0`. |
| `playjump`      | `deck`, `beat` (1-based)   | `onBeat`       | Jump to `beat` and start playing. |
| `bpm`           | `deck`, `value`            | `onBeat`       | Manually set BPM (pins it; persisted to the analysis DB). |
| `offset`        | `deck`, `value` (frames)   | `onBeat`       | Set the beat-grid origin offset in sample frames (persisted). |
| `nudge_offset`  | `deck`, `value` (frames)   | `onBeat`       | Nudge the grid origin by a frame delta (persisted). |
| `reanalyze`     | `deck`                     | `onBeat`       | Drop cached analysis and re-run BPM detection. |
| `metronome`     | `deck`                     | `on`, `onBeat` | Toggle the grid metronome click (`on` defaults to `true`). |
| `sync`          | `deck`                     | `on`, `onBeat` | Set this deck's beat-sync intent (`on` defaults `true`). The engine elects the master and phase-locks followers. |
| `master`        | `deck`                     | `onBeat`       | Force this deck to be the sync master (holds while it plays). |
| `eq_low`        | `deck`, `value` (0–1)      | `onBeat`       | Set low EQ (0.5 = unity). |
| `eq_mid`        | `deck`, `value` (0–1)      | `onBeat`       | Set mid EQ. |
| `eq_high`       | `deck`, `value` (0–1)      | `onBeat`       | Set high EQ. |
| `volume`        | `deck`, `value` (0–1)      | `onBeat`       | Set channel volume. |
| `align`         | `deck`, `subjectBeat`, `reference`, `referenceBeat` | — | See §6. Cross-deck phase alignment. **Not** schedulable with `onBeat`. |

The canonical field list lives in `engine/src/Control.hpp` (`struct
ControlAction`), and the verb→handler mapping is `Engine::dispatchAction` in
`engine/src/Engine.cpp`.

---

## 5. Scheduling with `onBeat`

Any single-deck verb may carry an `"onBeat": <beat>` field. Instead of running
now, the action is deferred until **that deck's** playhead reaches that (1-based)
beat. Example — drop the bass on deck 1 at beat 64, then bring it back at 96:

```json
{ "actions": [
  { "action": "eq_low", "deck": "d1", "value": 0.0, "onBeat": 64 },
  { "action": "eq_low", "deck": "d1", "value": 0.5, "onBeat": 96 }
] }
```

Scheduled actions are one-shot and re-arm if the playhead is scrubbed back before
the trigger beat. They also show up as markers on the deck's waveform in the GUI.
Implementation: `Engine::dispatchAction` registers a `ScriptTrigger` (see
`engine/src/Engine.hpp`) fired by `Engine::checkScriptTriggers`.

`align` is the one verb that ignores `onBeat` (it is handled before the
scheduling branch).

---

## 6. The `align` verb

`align` jumps the **subject** deck so that a chosen beat of it coincides *in time*
with a chosen beat of a **reference** deck.

```json
{ "action": "align",
  "deck": "d1",            // subject — the deck that jumps
  "subjectBeat": 32,
  "reference": "d2",       // reference — the deck that stays put
  "referenceBeat": 64 }
```

Reading: "make it so that when deck 1 is at beat 32, deck 2 is at beat 64." The
engine computes the subject's required position right now and jumps it there:

```
subjectBeatNow = currentBeat(reference) - (referenceBeat - subjectBeat)
```

(Only the *difference* `referenceBeat - subjectBeat` matters, so the 1-based vs
0-based convention cancels.) Because it preserves sub-beat phase, it lines the two
grids up exactly.

Caveats to document for users:
- It is a **one-shot phase jump**. The decks only *stay* aligned if their tempos
  are locked — pair `align` with `sync` (set both decks' sync intent so the
  follower tracks the master's tempo). Without sync they drift apart.
- No-op (silently) if the subject has no detected/zero BPM, the reference has no
  usable tempo, or either deck name is invalid.

Implementation: `Engine::actAlign` in `engine/src/Engine.cpp`.

---

## 7. Designing the Python REPL library

Goal: a library where each evaluated REPL line compiles to one (or a batch of)
JSON action(s) and POSTs them. Suggested ergonomics:

```python
from lazerdeck import Engine

eng = Engine()                 # defaults to http://127.0.0.1:8203
eng.health()                   # True if the engine is up

eng.d1.load("C:/music/a.wav")  # each call = one POST /action
eng.d1.play()
eng.d2.load("C:/music/b.wav")
eng.d2.sync(True)              # follow the master tempo
eng.d2.play()

# schedule: cut deck 2's lows at beat 64
eng.d2.eq_low(0.0, on_beat=64)

# phase-align d1 beat 32 to d2 beat 64
eng.align(subject=eng.d1, subject_beat=32,
          reference=eng.d2, reference_beat=64)

eng.d1.eject()

# batch several actions into one atomic POST
with eng.batch() as b:
    b.d1.load("C:/music/c.wav")
    b.d1.play()
```

Design notes:
- `eng.dN` returns a small `Deck` proxy bound to the deck name `"dN"`; its methods
  build an action dict and hand it to the engine to send.
- Each method maps 1:1 to a verb in §4. Use the §4 table to generate them (or
  write them out — there are ~20). Keep argument names Pythonic
  (`on_beat`, `value`) but emit the exact JSON keys (`onBeat`, `value`).
- For a REPL, send immediately per call. Provide a `batch()` context manager that
  accumulates actions and POSTs them once on exit for atomic multi-deck moves.
- **Client-side validation** (recommended, because the server won't tell you):
  check the verb is known, required fields are present, deck index is in range
  (`eng.deck_count` — though note there's no HTTP way to read it; assume 2, or
  make it configurable), beats ≥ 1, EQ/volume in `[0,1]`.
- Keep it dependency-light. `urllib.request` (stdlib) is enough; `requests` is
  fine if you prefer. Reuse a connection / set a short timeout.

### Minimal reference implementation (stdlib only)

```python
import json
import urllib.request
import urllib.error

DEFAULT_URL = "http://127.0.0.1:8203"

# Verbs that take a single positional value, and the JSON key it maps to.
_VALUE_KEY = {
    "seek": "value", "speed": "speed", "playjump": "beat",
    "bpm": "value", "offset": "value", "nudge_offset": "value",
    "eq_low": "value", "eq_mid": "value", "eq_high": "value", "volume": "value",
}
_NO_ARG = {"eject", "play", "pause", "stop", "speed_reset", "reanalyze", "master"}
_BOOL = {"metronome", "sync"}  # take an `on` bool (default True)


class Deck:
    def __init__(self, engine, name):
        self._engine = engine
        self.name = name  # "d1"

    def _act(self, action, **kw):
        a = {"action": action, "deck": self.name}
        a.update({k: v for k, v in kw.items() if v is not None})
        return self._engine._dispatch(a)

    def load(self, path, on_beat=None):
        return self._act("load", path=path, onBeat=on_beat)

    def __getattr__(self, action):
        # Auto-generate the simple verbs from the tables above.
        if action in _NO_ARG:
            return lambda on_beat=None: self._act(action, onBeat=on_beat)
        if action in _BOOL:
            return lambda on=True, on_beat=None: self._act(action, on=on, onBeat=on_beat)
        if action in _VALUE_KEY:
            key = _VALUE_KEY[action]
            return lambda value, on_beat=None: self._act(action, onBeat=on_beat, **{key: value})
        raise AttributeError(action)


class Engine:
    def __init__(self, url=DEFAULT_URL, deck_count=2):
        self.url = url.rstrip("/")
        self.deck_count = deck_count
        self._batch = None  # list while inside a batch() block
        for i in range(1, deck_count + 1):
            setattr(self, f"d{i}", Deck(self, f"d{i}"))

    def health(self):
        try:
            with urllib.request.urlopen(self.url + "/health", timeout=2) as r:
                return json.load(r).get("ok", False)
        except urllib.error.URLError:
            return False

    def align(self, subject, subject_beat, reference, reference_beat):
        return self._dispatch({
            "action": "align",
            "deck": subject.name, "subjectBeat": subject_beat,
            "reference": reference.name, "referenceBeat": reference_beat,
        })

    def batch(self):
        engine = self

        class _Batch:
            def __enter__(self_):
                engine._batch = []
                # expose the same deck proxies inside the block
                for i in range(1, engine.deck_count + 1):
                    setattr(self_, f"d{i}", getattr(engine, f"d{i}"))
                return self_

            def __exit__(self_, *exc):
                actions, engine._batch = engine._batch, None
                if actions and exc[0] is None:
                    engine._post({"actions": actions})
                return False
        return _Batch()

    def _dispatch(self, action):
        if self._batch is not None:
            self._batch.append(action)
            return None
        return self._post({"actions": [action]})

    def _post(self, payload):
        data = json.dumps(payload).encode()
        req = urllib.request.Request(
            self.url + "/action", data=data,
            headers={"Content-Type": "application/json"}, method="POST")
        try:
            with urllib.request.urlopen(req, timeout=3) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:        # 400 = parse/validation error
            return json.load(e)
```

This is a starting point, not a finished library — the agent should add explicit
typed methods (better autocomplete/help than `__getattr__`), client-side
validation, docstrings per verb, and tests. The `__getattr__` trick keeps the
example short; production code will likely prefer generated explicit methods.

---

## 8. Known gaps / things to flag upstream

- **No state read-back over HTTP.** A REPL ideally shows BPM, position,
  play/sync state. Today that requires the in-process FFI. Adding a
  `GET /state` (and maybe `GET /state/dN`) to `ControlServer` that serializes
  the deck states with Glaze would close this. Flag it if the library needs it.
- **Errors are async.** See §3 — runtime failures are not reported. Consider a
  future `validate`-only mode or per-action result array.
- **Port discovery.** The default is 8203 but the user can change it in the GUI.
  The library should let callers pass the URL/port.

---

## 9. C++ code map (where to look)

| Concern | File | Symbol |
|---------|------|--------|
| Wire schema (fields) | `engine/src/Control.hpp` | `ControlAction`, `ControlRequest`, `ControlResponse` |
| HTTP routes & JSON parse | `engine/src/ControlServer.cpp` | `ControlServer::start` (`POST /action`, `GET /health`) |
| Verb → handler dispatch | `engine/src/Engine.cpp` | `Engine::dispatchAction`, `Engine::submitActions` |
| Deck-name / beat parsing | `engine/src/Engine.cpp` | `parseDeckName` (anon ns); beat math in the `act*` helpers |
| Per-verb effects | `engine/src/Engine.cpp` | `actLoad`, `actEject`, `actPlay`, `actSeekSeconds`, `actPlayjump`, `actAlign`, … |
| Scheduling (`onBeat`) | `engine/src/Engine.cpp` / `.hpp` | `ScriptTrigger`, `checkScriptTriggers` |
| Track load / eject internals | `engine/src/Deck.cpp` | `Deck::load`, `Deck::eject` |
| Server lifecycle / port | `engine/src/ControlServer.{hpp,cpp}`, `Engine::startControlServer` | bind on 8203, `wait_until_ready` |
| C FFI (host/in-process) | `engine/include/lazerdeck/lazerdeck.h` | `lazerdeck_control_*`, `lazerdeck_get_deck_state` |

The control plane reuses the same typed `act*` handlers as the legacy text
"LazerScript" parser (`Engine::processCommands`), so behavior is identical
whether driven by JSON or the in-app script editor.

---

## 10. Quick smoke test (no Python needed)

```bash
# is it up?
curl -s http://127.0.0.1:8203/health

# load + play deck 1
curl -s http://127.0.0.1:8203/action -H "Content-Type: application/json" -d \
  '{"actions":[{"action":"load","deck":"d1","path":"C:/music/a.wav"},
               {"action":"play","deck":"d1"}]}'

# eject deck 1
curl -s http://127.0.0.1:8203/action -d '{"actions":[{"action":"eject","deck":"d1"}]}'
```
