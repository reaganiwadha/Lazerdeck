# Flutter host app

Located in `app/`. Plain Flutter desktop app (no FFI-plugin template) that
`add_subdirectory`s the engine from its platform CMake and binds it via
`dart:ffi`.

## Dart layers

| File | Role |
|------|------|
| `lib/ffi/lazerdeck_bindings.dart` | `DynamicLibrary` open + `lookupFunction`s; `LazerDeckState` struct mirror |
| `lib/ffi/engine.dart` | `LazerdeckEngine` (lifecycle, `loadFile/play/pause`, `deckState`) + `DeckState` snapshot |
| `lib/ui/decks_view.dart` | Polls deck state ~25 Hz; one card per deck (track, timecode, BPM, Open/Play/Pause) |
| `lib/main.dart` | Loads + inits the engine, builds the dark-themed app |

The library is opened by bare name (`lazerdeck_engine.dll` /
`liblazerdeck_engine.so`); the platform CMake bundles it next to the runner
(Windows) or in `lib/` on the runner's rpath (Linux).

## Native build integration

`app/windows/CMakeLists.txt` and `app/linux/CMakeLists.txt` each add, after the
runner subdirectory:

```cmake
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../engine" "${CMAKE_BINARY_DIR}/engine")
```

and bundle the library into the install:

```cmake
# Windows: next to the .exe
install(FILES "$<TARGET_FILE:lazerdeck_engine>"
  DESTINATION "${INSTALL_BUNDLE_LIB_DIR}" COMPONENT Runtime)
# Linux additionally: add_dependencies(${BINARY_NAME} lazerdeck_engine)
```

Because Flutter's `INSTALL` target builds everything, the engine (and its
FetchContent deps) compiles as part of `flutter run`/`flutter build`. No vcpkg,
no toolchain file, no extra steps.

## Dependencies (`pubspec.yaml`)

- `ffi` — `Pointer`/`Utf8` helpers (`package:ffi`).
- `file_picker` — native open dialog. **v11 API**: `FilePicker.pickFiles(...)`
  is a *static* method (not `FilePicker.platform.pickFiles`).

## UI behaviour (PoC)

- Two deck cards side by side.
- **Open** → `FilePicker.pickFiles(type: FileType.audio)` → `engine.loadFile`.
- **Play/Pause** → `engine.play/pause`.
- Timecode = `currentFrame / sampleRate` formatted `mm:ss.cs`, refreshed by a
  40 ms `Timer` polling `lazerdeck_get_deck_state`.
- `LOADING` / `ANALYZING` chips reflect the deck flags.

## Run

```
cd app
flutter run -d windows
```

## Future

- Waveform/beatgrid widgets once the engine exposes buffer/energy accessors
  (see [04-ffi-contract.md](04-ffi-contract.md)).
- Move polling to a `ValueListenable`/stream; consider `dart:ffi` `NativeCallable`
  for push updates instead of polling.
- A LazerScript console panel that calls `lazerdeck_push_command`.
