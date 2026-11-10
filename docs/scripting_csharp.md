# C# Scripting on the Server

Server resources can be written in C#, running on CoreCLR, next to the
existing Node.js resources. Each binding is declared once, and both languages
get it from that one declaration. This document records the design, what is
built, and what is left.

The client is out of scope: it keeps its standalone V8 engine. Projects built
on the Framework are out of scope too. They get C# for their own handle types
by declaring them the same way (see "Declaring a binding").

## Why

C# gives server scripts a typed, compiled language with the .NET ecosystem
behind it. FiveM (Mono) and alt:V (CoreCLR) both offer it beside JavaScript.

The cost to avoid is a second binding layer written by hand. Before this work,
every binding was a `v8pp::class_` chain with its documentation attached
inline. A C# surface written the same way would double every future binding
and drift from the JavaScript one.

## How others do it

### FiveM: schema first, generated per language

- Each runtime (Lua, V8/Node, Mono) is a component implementing the
  `IScriptRuntime` / `IScriptHost` interfaces declared in `fxScripting.idl`.
  A resource gets one runtime instance per language it uses.
- Every native goes through one `InvokeNative` entry point, keyed by a hash
  of its name, with a generic argument context. The C++ handlers register by
  name.
- Signatures live in `ext/native-decls` (markdown). A codegen suite turns
  them into Lua, JavaScript, C# and `.d.ts` wrappers.
- Events cross runtimes as msgpack. A function reference is a msgpack
  extension type carrying a reference id. Exports are built on events.
- The weakness: declarations and handlers are separate files, and nothing
  checks that they agree. FiveM's C# runs on Mono (mono v2 included), not
  CoreCLR.

### MTA:SA: C++ first, marshalling derived

- MTA:SA only has Lua, so it offers no lesson about running two languages
  side by side. The useful part is `ArgumentParser<&Func>`: the Lua binding
  is derived from the C++ function's signature (`std::optional`,
  `std::variant`, element pointers). One declaration, checked by the
  compiler.
- One VM per resource (`CLuaMain`). Cross-resource calls and events go
  through `CLuaArguments`, a list of neutral values.

### alt:V: JavaScript and C# on CoreCLR

- The C# module boots .NET through hostfxr (`AltV.Net.Host`) and creates a
  `CSharpResourceImpl` per resource.
- The C# API sits on a flat C API (`AltV.Net.CApi`), not on the C++ classes.

## The model

C++ stays the source of truth. This takes MTA:SA's idea of deriving the
binding from the C++ function, and FiveM's codegen, generated from the C++
declarations rather than from a separate schema.

### Declaring a binding

`Api::ClassBinding<T>` (`scripting/api/binding.h`) replaces the direct
`v8pp::class_` chain. Each member records:

- its C++ function, as a template argument so it can become a plain function
  pointer;
- its documentation;
- the side it exists on (`Api::Side`);
- the languages it is bound for (`Api::Target`).

`Entity` and `Player` are declared this way. `builtins/README.md` has the
conventions.

- **JavaScript.** `Build(isolate)` makes the v8pp class and records the same
  metadata the TypeScript contract is generated from, in declaration order.
  The migration left the exported catalog byte-for-byte identical.
- **Native.** Each member also registers a thunk in `Api::NativeCatalog`
  under a stable id: the CRC32 of `"<Class>.<member>"`, with `=` appended for
  a setter. This is the scheme entity type ids already use. No C++ is
  generated.
- **C#.** `Api::NativeCatalog::WriteCSharp` turns the same declarations into
  `managed/MafiaHub.Framework.Scripting/Generated/Bindings.g.cs`: `partial`
  classes that call the thunks through one `delegate* unmanaged`.
  - The file is committed. The `script_bindings` test fails while it is
    stale; `FW_UPDATE_GENERATED=1` regenerates it.
- A member written against V8 directly (`Entity.rotation`'s union-typed
  setter, `setVisibleTo`) is a `JavaScript(...)` hook. When C# needs it, a
  typed equivalent is declared `Target::Native`.

### The flat convention

One thunk shape for every member:

```cpp
int32_t thunk(uint64_t self, const NativeValue *args, int32_t argc, NativeValue *result);
```

`NativeValue` (`scripting/api/native_abi.h`, mirrored in
`Interop/NativeValue.cs`) is a 24-byte tagged value:

| C++ | Across the thunk | C# |
|---|---|---|
| `bool`, `int32_t`, `uint32_t`, `int64_t`, `uint64_t`, `float`, `double` | as is | `bool`, `int`, `uint`, `long`, `ulong`, `float`, `double` |
| `std::string` | UTF-8 pointer + length | `string` |
| `Vector2/3/4`, `Quaternion`, `Color` | four floats | `readonly record struct`, convertible to `System.Numerics` |
| handle (`Entity`, `Player`) | `uint64` network id | class carrying the id |

The thunk rebuilds the receiver from its id for each call. When that fails,
the receiver is stale: the call does nothing and the result is the type's
default, as the JavaScript rule says. Other failures come back as a status
and a message. `Api::NativeTraits` is specialised per type. A member whose
types have no specialisation cannot be bound for C#, and binding it so fails
at startup, naming it.

### Errors

The builtin rules become a neutral error kind thrown by the C++ body
(`Api::ThrowArgumentError`, `Api::ThrowStateError`). Each backend maps it:

| Kind | JavaScript | C# |
|---|---|---|
| argument | `TypeError` | `ArgumentException` |
| state | `Error` | `InvalidOperationException` |
| stale receiver | silent no-op | silent no-op, default result |

### Runtime per resource

`ScriptRuntime` (`scripting/script_runtime.h`) plays the part of FiveM's
`IScriptRuntime`. The manifest names a resource's server language:

```json
{
  "name": "bank",
  "version": "1.0.0",
  "mafiahub": {
    "serverRuntime": "csharp",
    "serverScripts": ["bin/Bank.dll"],
    "clientScripts": ["client.js"],
    "exports": ["balance"]
  }
}
```

- `serverRuntime` defaults to `"js"`. Client scripts are always JavaScript.
- `sharedScripts` are refused, because they would run on the server too.
- `ResourceManager` keeps everything else as it is for JavaScript:
  dependencies, states, restarts, and the `resourceStart`/`resourceStop`
  events.
- For a resource in another runtime, it resolves the server scripts, with the
  same containment checks, and hands them to that runtime.

Values cross as `TransferredValue`, the copies JavaScript resources already
exchange:

- **Events.** Every emission on the bus also reaches runtimes that listen for
  it, with the arguments copied once (`Events::SetForeignListener`).
  - A C# handler returning `false` vetoes a synchronous server event.
  - Client events reach only `OnClient` handlers, in every language.
- **Exports, C# to JavaScript.** `ResourceManager::CallExport` calls a
  JavaScript export inside its own runtime.
- **Exports, JavaScript to C#.** `Exports.get` on a C# resource returns a
  stand-in that copies its arguments across.
- **Encoding.** Between C++ and C#, values are encoded by `Clr::ValueCodec`,
  mirrored in `Interop/ValueCodec.cs`.

### Functions and answers that come later

Functions cross in both directions, and so do calls that answer later.
Everything meets in the server's own JavaScript runtime (the host). There,
the existing `FunctionReferences` carry a function or a promise to any
resource's runtime.

- **JavaScript function to C#.** The server keeps the function's copy under a
  handle, and C# gets a `ScriptFunction` for it. A parameter typed as a
  delegate (`Func<double, double>`, `Func<int, Task<string>>`) gets one
  converted. C# releases the handle when the `ScriptFunction` is collected.
- **C# delegate to JavaScript.** The delegate gets an id in the C# host. The
  server wraps it in a host function, which crosses like any other. The
  wrapper is weak, and when JavaScript lets go of it the delegate is released.
  A wrapper that comes back to C# is recognised by a V8 private mark and
  arrives as the original delegate.
- **Answers that come later.** Every call between the languages answers
  with a value, an error, or a pending token (`clr_interop.h`). The side that
  answered pending settles the token later.
  - A promise in the host settles its token when it settles.
  - A C# task settles its token from the resource's scripting context.
  - `ResourceManager::AwaitInJavaScript` turns a pending answer into a promise
    in the host, which reaches the caller through its stand-in.
  - `Exports.CallAsync` and `ScriptFunction.InvokeAsync` turn one into a
    `Task` in C#.
- **Stopping.** A C# resource that stops rejects what it still owed
  (`"resource 'x' stopped before answering"`), and its delegates stop
  answering. A JavaScript resource that stops makes its functions throw.

### Stopping a C# resource

CoreCLR cannot end another thread, and .NET cannot list the sockets a
resource opened. Running each resource in its own process could, but every
binding call would then cross processes, which a per-tick game API cannot
afford. So the stop is cooperative, and the server checks that it worked:

1. `ServerScript.Stopping` is cancelled.
2. `OnStop` runs.
3. Everything passed to `Track` / `TrackAsync` is disposed, newest first.
4. `Background` work gets `stopGraceMs` (1 s by default) to see the
   cancellation and end; whatever is still running is named in a warning.
5. The resource's timers, handlers, exports, delegates and queued
   continuations are dropped. Its pending answers are rejected.
6. Its `AssemblyLoadContext` is unloaded. The host collects until the
   assemblies are gone. If they are still loaded after `unloadCheckMs`
   (10 s by default), it logs a warning naming the resource and its
   unfinished work. `ClrRuntime::GetUnloadingResources()` lists such
   resources.

Leftover code cannot act on the server either:
- Its continuations, timers and functions are gone.
- A worker thread cannot call the scripting API.

### Hosting

- **Starting .NET.** `Clr::ClrRuntime` finds hostfxr (`DOTNET_ROOT`, then the
  platform's install locations). It starts .NET from the host's
  `runtimeconfig.json` with the first C# resource, so a server without one
  never loads .NET.
- **Lifetime.** CoreCLR runs once per process and stays.
- **Entry point.** The host assembly, `MafiaHub.Framework.Scripting`, exposes
  one `[UnmanagedCallersOnly]` entry point. Through it the server and the host
  exchange function tables (`clr_interop.h`, mirrored in `Interop/HostApi.cs`).
- **Isolation.** Each resource gets a collectible `AssemblyLoadContext`,
  loaded from memory so the files can be replaced, and unloaded when the
  resource stops.
- **Threading.**
  - Each resource has a `SynchronizationContext` that runs its continuations
    on the scripting thread from `ResourceManager::TickRuntimes()`.
  - Calling the scripting API from another thread throws.
- **Build and shipping.**
  - CMake builds the host with the .NET SDK (`FrameworkScriptingCSharp`, on by
    default when a .NET 10 SDK is installed; `FW_SCRIPTING_CSHARP`).
  - The SDK ships it as `runtime/<arch>/server/dotnet/`.
  - The server looks for it in `dotnet/` beside its binary.

## Writing a C# resource

Reference `MafiaHub.Framework.Scripting.dll` (from the SDK's `dotnet/`) without
copying it, and subclass `ServerScript`. The server creates one instance of
every non-abstract subclass in the resource's assemblies:

```csharp
using MafiaHub.Framework.Scripting;

public sealed class Bank : ServerScript
{
    protected override void OnStart()
    {
        Exports.Register("balance", (Player player) => Balances.Get(player.SteamId));
        Events.On("playerConnected", (Player player) => Log.Info($"{player} connected"));
        Events.OnClient("bank:deposit", async (Player player, double amount) =>
        {
            await Ledger.WriteAsync(player.SteamId, amount); // resumes on the scripting thread
            Events.Emit("bank:changed", player, amount);
        });
    }
}
```

- Event handlers and exports are any delegate. Their parameters are filled
  from the arguments and converted: a number arrives as `double` and converts
  to any numeric type, an array to an array or a `List<T>`, an object to a
  `Dictionary<string, T>`, a function to a `ScriptFunction` or a delegate
  type.
- `Events.On` returns an `IDisposable`.
- An export may return a `Task`: JavaScript gets a promise.
  - `Exports.CallAsync` awaits another resource's promise or task.
  - `Exports.Call` throws when the answer is not ready.
- Timers that end with the resource: `Timers.After` and `Timers.Every`, the
  counterparts of `setTimeout` and `setInterval`.
- Work off the scripting thread goes through `Background(name, token =>
  ...)`, and what needs disposing goes through `Track(...)`. Both are ended
  by the stop.
- Delays and I/O should take `Stopping`. Without it they keep the resource's
  code loaded until they end, and the server reports that.

## Plan

1. **Neutral descriptor and V8 backend.** `Entity` and `Player` were
   migrated; the exported catalog is unchanged byte for byte. *(done)*
2. **Error kinds.** `Api::ScriptError`; the V8 backend maps them;
   `builtins/README.md` has the table. *(done)*
3. **Runtime selection.** `ScriptRuntime`, `serverRuntime` in the manifest,
   and routing through `ResourceManager`. *(done)*
4. **CoreCLR hosting.** hostfxr, one `AssemblyLoadContext` per resource, the
   tick-driven `SynchronizationContext`, and a test resource. *(done)*
5. **CLR thunks and C# codegen.** The thunk table and the generated C# API,
   kept current by a test. *(done)*
6. **Cross-runtime service APIs.** Events and exports both ways over
   `TransferredValue`. *(done)*
7. **Functions and later answers.** Functions in both directions, and
   promises and tasks settling across languages. *(done)*
8. **Stopping C# resources.** Cooperative stop, enforcement, and unload
   verification. *(done)*

Tests: `script_bindings`, `clr_value_codec`, `script_runtime` (a fake runtime
drives the routing) and `csharp_scripting` (the real CoreCLR beside Node,
built when the .NET SDK is present). The last one includes a resource that
leaves a thread running, to check the unload warning.

## Open questions

- **Stopping is cooperative.** A thread or socket a C# resource opened
  without `Stopping`, `Track` or `Background` outlives its stop. The server
  reports it, but cannot end it (see "Stopping a C# resource"). Only one
  process per resource would. That is not planned, given what it costs every
  binding call.
- **Functions need isolated resources.** Functions cross through the host's
  `FunctionReferences`, which a server always has. An engine whose resources
  share one runtime refuses functions, naming where.
- **The rest of the surface.** State bags, `Chat`, `Messages`, text labels and
  the other service APIs are JavaScript only. Handle types reach C# as they
  are declared through `ClassBinding`. Service APIs need a C# layer each, as
  events and exports got.
- **Thunk cost.** Not measured yet against a v8pp call and against a generic
  invoke entry point like FiveM's `InvokeNative`.
- **Runtime distribution.** The server requires an installed .NET 10 runtime
  (`RollForward` is `Major`). Shipping one with the server is not decided.
- **Versioning.** Adding C# is a MINOR bump. The generated C# API joins the
  `.d.ts` as part of the scripting contract.

## Sources

- [FiveM: Script runtimes (fxOM, `fxScripting.idl`, `IScriptRuntime`)](https://docs.fivem.net/docs/developers/script-runtimes/)
- [FiveM: `prebuild_natives.cmd` (native codegen to C#, JavaScript, Lua)](https://www.github.com/citizenfx/fivem/blob/master/prebuild_natives.cmd)
- [FiveM: About native functions](https://docs.fivem.net/docs/scripting-manual/introduction/about-native-functions/)
- [FiveM mono v2 setup](https://github.com/thorium-cfx/mono_v2_get_started/blob/main/Setup.md)
- [MTA:SA wiki: Lua API (`ArgumentParser`)](https://github.com/multitheftauto/mtasa-blue/wiki/Lua-API)
- [alt:V C#: internal information (hostfxr, `CSharpResourceImpl`)](https://docs.altv.mp/cs/articles/internal-information.html)
- [alt:V coreclr-module](https://www.Github.com/FabianTerhorst/coreclr-module)
- [AltV.Net.CApi](https://packages.nuget.org/packages/AltV.Net.CApi/16.1.1)
