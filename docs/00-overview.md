# Lazerdeck — Overview

Lazerdeck is a DJ/deck audio engine. As of this refactor it is split into a
**headless C++ engine** (the brains) and a **Flutter desktop host** (the UI),
talking over a small C ABI via `dart:ffi`. VST3 hosting is an **optional,
dynamically-loaded plugin** the engine loads at runtime if present.

## Why this shape

The previous build welded SDL2 (rendering) and Qt6 (the LazerScript editor)
directly into the engine, and compiled the Steinberg VST3 SDK in unconditionally.
That made the engine heavy, hard to build, and expensive to reason about. The
goals of this refactor:

- **Flutter is the host.** All UI moves to Flutter; the engine is headless.
- **One self-contained CMake.** Every native dependency comes via FetchContent —
  no vcpkg, no system packages, no Qt, no SDL. Flutter's desktop build compiles
  the engine directly with zero toolchain injection.
- **VST3 is optional and decoupled.** It lives in its own shared library behind a
  C ABI. Missing library ⇒ VST features are silent no-ops.
- **Modular directory layout** so future agents (and humans) can navigate fast.

## The pieces

| Component | Path | What it is |
|-----------|------|------------|
| Engine | `engine/` | Headless C++ lib `lazerdeck_engine` (decks, audio, mixer, OSC) + C FFI |
| VST3 plugin | `plugins/vst3/` | Optional `lazerdeck_vst3` shared lib (the only VST3 SDK consumer) |
| Flutter app | `app/` | Desktop host; binds the engine via `dart:ffi` |
| Docs | `docs/` | This planning material |
| Retired UI | `legacy/qt-sdl-ui/` | Old SDL `Renderer` + Qt `ScriptEditor` (out of the build) |

## PoC scope (what works now)

Open an audio file into a deck, play/pause it, and watch deck state + a live
timecode in Flutter. Two decks render independently. VST3 is **not built** for the
PoC; the engine logs "VST support disabled" and audio passes through.

See [08-poc-milestones.md](08-poc-milestones.md) for acceptance criteria and
[07-build-and-tooling.md](07-build-and-tooling.md) to build and run.

## Read next

- [01-architecture.md](01-architecture.md) — data/threading model
- [02-directory-structure.md](02-directory-structure.md) — where things live
- [03-engine-headless-refactor.md](03-engine-headless-refactor.md) — what changed in the engine
- [04-ffi-contract.md](04-ffi-contract.md) — the C ABI surface
- [05-vst3-dynamic-plugin.md](05-vst3-dynamic-plugin.md) — the runtime VST3 boundary
- [06-flutter-app.md](06-flutter-app.md) — the Dart side
