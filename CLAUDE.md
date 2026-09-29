# IL2CPP-Dumper

Windows-only C++17 tool: an injector (`Injector/`) loads a dumper DLL (`Dump/`) into a Unity game.
The DLL reads type metadata through the IL2CPP (or Mono) runtime exports and writes C# / JSON files.

## Build

Visual Studio 2022 (v143). x86 and x64 are both built because the DLL must match the game's bitness.

```cmd
msbuild Dump.sln /p:Configuration=Release /p:Platform=x64
msbuild Dump.sln /p:Configuration=Release /p:Platform=x86
```

Outputs land in `bin\<Platform>\Release\`. There is no way to build or run the Windows code on macOS/Linux.

## Tests

Only the runtime-independent JSON layer is testable off-Windows:

```bash
clang++ -std=c++17 -Wall -Wextra -IDump/include tests/model_test.cpp Dump/src/Model.cpp -o model_test && ./model_test
```

Anything that touches `Windows.h` (everything else in `Dump/`) can only be verified by injecting into a real game.

## Layout

| Path | Role |
|------|------|
| `Dump/include/Model.h`, `Dump/src/Model.cpp` | `ClassData` structs + the **only** JSON serializer. No Windows headers. |
| `Dump/src/Dumper.cpp` | Collects `ClassData` from IL2CPP / Mono, writes files (`AssemblyJsonWriter`), filters, index.json, C# output, memory-scan fallback. |
| `Dump/src/IL2CPP_API.cpp`, `Mono_API.cpp` | `GetProcAddress` wrappers. Every export is looked up dynamically and every wrapper is null-safe. |
| `Dump/src/Utils.cpp` | `IL2CPP_Dumper.cfg` parsing, output dir, `AccessModifier`. |
| `Dump/src/Main.cpp` | In-game GUI (Win32) and headless `auto=` mode. |
| `Injector/` | GUI injector (manual map), launches/attaches to the game, picks the x86/x64 DLL. |
| `docs/SCHEMA.md` | JSON output schema. Update it together with `Model.cpp`. |

## Conventions and gotchas

- **JSON is produced only by `Json::SerializeClass`.** Do not hand-write JSON with `operator<<` in exporters (that is how escaping bugs and IL2CPP/Mono drift happened before). Changing the layout means: edit `Model.cpp`, bump `kSchemaVersion` if it is breaking, update `tests/model_test.cpp` and `docs/SCHEMA.md`.
- Runtime exports differ between Unity versions. Required ones gate `Initialized`; everything added later (properties, enum, static values, flags) is optional and must degrade to "field missing from output", never a crash.
- Reads of game memory that can fault (`il2cpp_field_static_get_value`, reading `MethodInfo::methodPointer`) are wrapped in `__try/__except` helpers with no C++ objects in scope.
- Member access uses ECMA-335 values (`flags & 7`): 1 private, 2 private protected, 3 internal, 4 protected, 5 protected internal, 6 public.
- Code comments in the repo are a mix of English and Korean; match the file you are editing.
- Config is a one-shot file `IL2CPP_Dumper.cfg` next to the game exe (deleted after read). See `Utils.h` for keys. `auto=ai|human|mono` runs without a GUI and writes `IL2CPP_Dump_DONE.txt` when finished.
