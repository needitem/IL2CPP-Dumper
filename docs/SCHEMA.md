# JSON output schema (version 2)

Written by `Json::SerializeClass` in `Dump/src/Model.cpp`. Both the IL2CPP and Mono exporters use it.

## Reading a dump (for humans and LLMs)

1. Open `index.json` in the output folder. It lists every assembly, the file(s) that hold it, the class
   count and a namespace histogram, so you can pick what to read without loading everything.
2. Open the assembly file. **Each class is on its own line**, so `grep`, line ranges and partial reads work.
3. Large assemblies are split every `chunk` classes (default 1000): `X.json`, `X.part2.json`, ...
   The index lists all parts.

## Folders

| Folder | Content |
|--------|---------|
| `IL2CPP_Dump_JSON\` | Full: token, field offsets, method RVAs, access |
| `IL2CPP_Dump_Summary\` | Compact: no token/offset/RVA, `public` access omitted, compiler-generated methods dropped |
| `Mono_Dump_JSON\` | Mono runtime, Full layout (no RVA, no enum values) |

## index.json

```json
{"schemaVersion":2,"runtime":"il2cpp","mode":"full","assemblies":[
{"assembly":"Assembly-CSharp","classCount":812,"files":["Assembly_CSharp.json"],"namespaces":{"(global)":40,"Game":300}}
]}
```

## Assembly file

```json
{"schemaVersion":2,"runtime":"il2cpp","assembly":"Assembly-CSharp.dll","part":1,"classes":[
{...class...},
{...class...}
]}
```

## Class object

| Key | Meaning |
|-----|---------|
| `name`, `fullName` | Simple and namespace-qualified name |
| `type` | `class`, `struct`, `interface`, `enum` |
| `token` | Metadata type token (Full only) |
| `extends` | Base type **full name**; omitted for `System.Object` / `ValueType` / `Enum` |
| `implements` | Interface full names |
| `underlying` | Enum underlying type |
| `static` / `abstract` / `sealed` | `static` = abstract+sealed. Only present when true |
| `fields[]` | see below |
| `properties[]` | `name`, `type`, `get`, `set`, `static`, `access` (access comes from the accessor) |
| `methods[]` | see below |

### Field

`name`, `type`, `access` (omitted in Summary when `public`), `static`, `const` (only when true),
`value` (enum members and integer constants, IL2CPP only), `offset` (Full only; instance and static-storage
fields, not constants).

Enum members are listed as fields with `value`; their `type` is omitted because it is the enum itself.

### Method

`name`, `returns`, `params[]` (`type`, `name`), `access`, `static`, `abstract`, `virtual`, and in Full
mode **`rva`**: hex offset from the IL2CPP module base (`GameAssembly.dll`), usable directly in
IDA/Ghidra after rebasing. Missing when the method has no native body (abstract, generic, stripped)
or under Mono.

## Access values

`public`, `protected internal`, `protected`, `internal`, `private protected`, `private`.

## Changes from version 1

- Every file has `schemaVersion`; index.json added; large assemblies split into parts.
- New: `implements`, `properties`, `underlying`, enum `value`, method `rva`, `static`/`const`/`abstract`/`virtual`.
- `extends` is now a full name (was the short name).
- `type` can now be `enum`.
- Access `internal` and `protected` were swapped for some flag values before; fixed.
- Strings are fully JSON-escaped (control characters used to be emitted raw).
- Files are one class per line instead of pretty-printed.
