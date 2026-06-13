# Pre-ship Architecture Roadmap

Lazerdeck (headless C++ engine + Flutter host + Python/HTTP control plane) is
close to shippable. The core is sound — coherent per-buffer signal chain,
star-topology sync, working HTTP + Python control plane. The remaining risk is
concentrated in a handful of **missing abstractions** that are cheap to add now
and expensive to retrofit after release, especially where future automation
agents add **effects, a mixbus, and cues**.

Posture is **balanced**: do the ship-blocking stability fixes plus the one
foundational abstraction (the effect interface) that is costly to add later;
defer the larger graph/sync reworks to post-launch but design them now so the
near-term work doesn't conflict with them.

> Trace basis — verify line numbers before editing; the engine was recently
> rewritten ("new engine" commits).
>
> - Audio path: `engine/src/AudioEngine.cpp` (`audioCallback`, `softLimit`)
> - Mixer: `engine/src/Mixer.{hpp,cpp}` (`MixerChannel::process`), `engine/src/ThreeBandEQ.hpp`
> - Deck DSP: `engine/src/Deck.cpp` (`process`, `seek`, RubberBand path)
> - Effects ABI: `engine/include/lazerdeck/vst3_abi.h`, `engine/src/Vst3Runtime.*`
> - Sync: `engine/src/Engine_Sync.cpp` (`updateMaster`, `updateSync`)
> - Control plane: `engine/src/Control.hpp`, `engine/src/ControlServer.cpp`,
>   `engine/src/Engine.cpp` (`dispatchAction`, `act*`, `processCommands`)
> - Cues: `engine/src/Trigger.hpp` (`DeckTrigger`), `Deck.cpp` (`addTrigger`,
>   `clearLoop`), `engine/src/AnalysisDB.hpp`
> - Clients: `engine/include/lazerdeck/lazerdeck.h`, `app/lib/ffi/*`,
>   `python/lazerdeck/client.py`

---

## Findings — what bottlenecks each area

### Effects + Mixbus
- **No `Effect` abstraction — only "VST3 slot."** `MixerChannel::process`
  hardcodes its stages (deinterleave → `ThreeBandEQ` bespoke → VST chain →
  fader). A built-in filter/echo/comp can only be added by writing a whole VST3
  *or* hand-splicing another bespoke stage. No interface that both built-in DSP
  and VST plugins satisfy.
- **No bus/graph:** the master is inlined in `audioCallback` (clear → sum decks →
  metronome → soft-limit). Master FX/EQ/comp or any aux send/return means editing
  the callback by hand. No `Bus`, no `send`, no routing object.
- VST params are addressed by integer index only — no named/discoverable params.

### Agent control-plane
- `ControlAction` is a flat bag of overloaded optionals (`value`/`beat`/`speed`)
  dispatched via a ~20-branch if-else in `dispatchAction`. No verb registry, no
  per-verb param validation in one place, no param discovery.
- Adding one verb touches: schema comment, dispatcher, (often) an `act*` handler,
  FFI header + impl, Dart bindings, Python client, and the legacy text parser.
- **Cues are absent from JSON/FFI/Python** — legacy text commands only. Agents
  cannot use them.
- `/action` is fire-and-forget: `ControlResponse` reports parse/validation only,
  not per-action runtime outcome. Agents drive blind.

### Sync
- Math is fine; the architecture is entangled. `updateSync` reaches into `Deck`
  internals and corrects phase by **riding `setSpeed`**, which resamples → pitch
  wobble (no key-lock). It recomputes from scratch every ~5 ms with no BPM
  filtering, no rate-flip hysteresis (a source tempo nudge can jump a follower
  0.9→1.8× audibly), a tempo-dependent deadband, a magic 0.25-beat hard-snap, and
  a snap-on-resume. There is no **transport / beat-clock** the decks lock to.

### RT stability
- Audio thread takes locks: `Deck::process` (`stretcherMutex`) and
  `MixerChannel::process` (`vstMutex`). VST load / chain restructure on the engine
  thread can block/glitch audio (priority inversion). More effects/buses amplify.
- No metering/clip detection beyond the soft-limiter.
- `seek()` writes `currentInputTime` without full serialization vs `process()`.

---

## Phase 1 — Ship-blocking (do before release)

### 1A. Lock-free audio-thread chain swaps (RT stability)
Replace the `vstMutex`/`stretcherMutex` held *inside* the audio callback with a
publish/retire pattern: the engine thread builds a new chain/state off-thread and
publishes a pointer the audio thread reads with `acquire`; the old object is
retired on the engine thread (deferred free) after the audio thread has advanced
past it. Mirror the existing atomic-scalar convention already used for
`speed`/`playing`/`currentFrame`. Keep `Deck::process`'s mutex only for
load-time buffer realloc, never for steady-state reads.

*Files:* `Mixer.cpp`, `Deck.cpp`, `AudioEngine.cpp`.

### 1B. Per-action result feedback (control-plane correctness)
Extend `ControlResponse` to carry a per-action result array (index, ok, error)
so agents can tell what actually executed. Execution is async on the engine
thread, so the minimal viable version validates each action *synchronously*
(deck exists, params in range, file exists) and reports that array in the POST
reply, keeping effect execution async. (Builds on the new `/state` read-back.)

*Files:* `Control.hpp`, `ControlServer.cpp`, `Engine.cpp` (`submitActions`,
`dispatchAction`), `python/lazerdeck/client.py`.

### 1C. Cue model: persist + decouple from triggers (cue correctness)
Split "hot cue" (named/numbered position) from "scriptable trigger." Give cues
their own per-deck store, persist them in `AnalysisDB` keyed by the same file
hash as bpm/offset (extend `AnalysisData`), and stop `clearLoop()` from wiping
cues. Fix the UI `hasCue => loopStart > 0` fake to read real cue state.

*Files:* `Trigger.hpp` (or new `Cue.hpp`), `Deck.{hpp,cpp}`, `AnalysisDB.hpp`,
`app/lib/ffi/engine.dart`.

### 1D. Foundational `IAudioEffect` interface (costly to retrofit — do now)
Introduce a minimal in-process effect interface so built-in DSP and VST plugins
are interchangeable in a channel's chain:

```cpp
struct IAudioEffect {
  virtual void process(float* const* io, int frames) = 0;  // in-place, deinterleaved
  virtual void setParam(int idx, float v) = 0;             // 0..1 normalized
  virtual int  paramCount() const = 0;
  virtual const char* paramName(int idx) const = 0;        // discovery
  virtual ~IAudioEffect() = default;
};
```

Wrap the existing VST3 slot as a `Vst3Effect : IAudioEffect` adapter (over
`LzrVst3Api`) and make `ThreeBandEQ` an `IAudioEffect` too. `MixerChannel` holds
`vector<unique_ptr<IAudioEffect>>` swapped lock-free per 1A. This unblocks
Phase 2's built-in effects and master bus without re-touching the audio path.

*Files:* new `engine/src/IAudioEffect.hpp`, `Mixer.{hpp,cpp}`, `Vst3Runtime.*`,
`ThreeBandEQ.hpp`.

---

## Phase 2 — Post-launch foundational (design now, build after ship)

### 2A. Bus / routing graph + real master bus
Promote the master from inlined callback code to a `Bus` object that owns an
`IAudioEffect` chain (master EQ/comp/FX) and a fader. Add channel→bus `send`
levels (aux returns). `audioCallback` becomes: render decks → channel chains →
sends → bus chains → master → limiter, all data-driven by the graph. Built on
Phase 1D's interface so no effect code changes.

### 2B. Effect/cue verbs + verb registry over the control plane
Replace the if-else dispatcher with a small registry mapping verb → {param spec,
handler}. Schema validation and param discovery fall out of the spec (also powers
agent introspection: a `GET /verbs`). Add effect verbs (add/remove/move effect,
set param by name, load VST, master-bus FX) and cue verbs (set/recall/delete cue)
on top of Phase 1C's cue model. Surface through FFI + Python.

---

## Phase 3 — Post-launch sync rework

### 3A. Transport / beat-clock abstraction
Introduce a `MasterClock` (continuous beat phase + tempo) that decks phase-lock
to, decoupling "what time is it musically" from "how fast is this deck's
resampler." `updateSync` consumes the clock instead of reaching into peer-deck
internals.

### 3B. Key-lock-aware, filtered phase correction
Use RubberBand's pitch-preserving mode so phase correction (riding tempo) no
longer wobbles pitch. Add: low-pass on detected BPM, hysteresis on the half/
double rate flip, a tempo-*independent* deadband (in milliseconds, not beats),
and quantized re-engage to remove snap-on-resume. Replace the magic 0.25-beat
hard-snap with a phase-ramp that's audibly smooth.

---

## Sequencing rationale (balanced posture)
- **1A + 1D are the keystone:** removing audio-thread locks and adding the effect
  interface are both far cheaper before there are many effects/buses to migrate.
- **1B + 1C** are correctness/UX gaps that bite agents and users on day one.
- Phases 2–3 are large but every Phase-1 choice is designed so they slot in
  without rework (graph reuses `IAudioEffect`; registry reuses cue model; sync
  reuses the existing rate-invariant `deckBeatPhase`).

---

## Verification (for when Phase 1 is implemented)
- Standalone engine build: `cmake --preset windows-msvc && cmake --build
  build-windows --target lazerdeck_engine` (use VS's CMake 3.31, not PATH's 4.3).
- Flutter host: `cd app && flutter run -d windows`.
- **RT-safety (1A):** load/swap a VST while audio plays; confirm no dropouts
  (long playing track; watch for xruns/clicks at chain swap).
- **Control feedback (1B):** POST a batch with one invalid action via the Python
  client; assert the response result array flags exactly that index.
- **Cues (1C):** set cues, eject + reload the same file, assert cues restored from
  `AnalysisDB`; set a loop then `clearLoop` and assert cues survive.
- **Effect interface (1D):** confirm `ThreeBandEQ`-as-`IAudioEffect` and a VST in
  the same channel chain produce identical output to the pre-refactor path (null
  test on a rendered buffer).
