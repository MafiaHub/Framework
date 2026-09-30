# Native input integration

Native multiplayer controls and scripted `Key` bindings share the framework's
input primitives. Game adapters keep native device acquisition, control locking,
cursor presentation and engine action-map policy.

## One query API

All native consumers and scripted `Key` bindings query an `IInput` instance:

```cpp
input->IsKeyDown(FW_KEY_SPACE);
input->IsKeyPressed(FW_KEY_F9);
input->IsKeyDown('W');
```

Use the existing `FW_KEY_*` macros from `<input/input.h>`. Letters and digits
use their uppercase character codes. Every adapter interprets printable codes
as US physical positions, independent of keyboard layout. Native scan-code and
layout conversions stay inside the backend. There is no public physical-key
query helper, provider opt-in or alternate key-code space.

`IsAvailable()` reports whether the source has fresh state. Held queries return
false while focus or native acquisition is unavailable; edge detectors seed
held state when availability returns so held keys do not become new presses.
Gameplay policy is separate: use the client's `IsLocalInputAvailable()` for
controls which must stop while a menu owns gameplay input.

## Window and UI sources

`PollingInput` implements the query API through foreground-aware OS acquisition.
Its polled press/release snapshots are advanced by `Update()` before consumers
read them. Initial acquisition and foreground regain seed state without new
presses. It also supplies ordinary Win32 cursor operations; game adapters can
override those with their native cursor/control-lock policy.

`WindowInput` adds window-message press/release edges, including taps shorter
than a frame. Derive the game adapter from it and forward every window message
to `ProcessEvent` before a UI handler consumes it. Call `Update()` after all
consumers have read the frame's edges. Held queries use the same live OS source,
so they also detect keys held before focus returned. Message decoding tracks
both modifier sides, stable mouse-button indices and signed client coordinates.
Focus loss clears pending presses and releases tracked event state for cleanup.
Focus acquisition seeds event state from held keys, including both modifier
sides, so subsequent messages do not manufacture an aggregate modifier press.
Character and IME messages remain on the text/UI path.

A native device adapter implements the same `IInput` contract using physical
positions and declares itself unavailable when acquisition is paused. Use
`PhysicalKeys::ToDirectInputCode` inside DirectInput adapters. The layout
translation used by Win32 polling is an internal backend operation.

UI controls which must remain live while gameplay devices are paused can own a
`PollingInput` source and use exactly the same query methods. M2O does this for
its debug/chat/escape hotkeys and key capture; gameplay and voice use its native
adapter. Source lifetime belongs to the feature/application, not to a query
flag or a compatibility switch.

## Resource-owned controls

`ResourceHolds` stores ownership, not native locking operations. Use
`Acquire`/`Release` for counted APIs, or `Set` for idempotent enable/disable APIs.
At resource stop/error, `TakeAll(resource)` returns exactly how many native
holds that owner must release. Clear the ledger whenever a forced cleanup also
drains native holds. A resource cannot release another owner's holds.

Resource callbacks and native transitions stay in the project's lifecycle
adapter. Engine action-map bindings remain appropriate for controls which
inherit native rebinding, priority, combat or inventory behavior.

## Unit tests

`FrameworkInputTests` has no renderer, browser, server or game dependency. Its
portable suite covers transitions, repeat filtering, frame edge retention,
availability resynchronization and resource ownership. Windows additionally
covers actual window messages, modifiers, mouse masks, common query behavior
and physical-to-native key mapping. The same modules run in `FrameworkTests`.

Linux, using the canonical build:

```sh
cmake --build build --target FrameworkInputTests
build/bin/FrameworkInputTests
```

Windows, using the canonical build script:

```bat
builds\build.bat FrameworkInputTests 64
builds\build-64\bin\FrameworkInputTests.exe
```
