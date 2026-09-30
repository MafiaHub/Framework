# Native input integration

Native multiplayer controls and scripted `Key` bindings share the framework's
input primitives. Game adapters keep native device acquisition, control locking,
cursor presentation and engine action-map policy.

## Window-message adapters

Derive the game adapter from `Framework::Input::WindowInput` and implement the
remaining cursor and control-lock methods. Forward every window message to
`ProcessEvent` before a UI handler can consume it. Call `Update` once, after all
consumers have read that frame's pressed/released edges.

The backend normalizes printable keys to US physical positions, filters
repeats, tracks both sides of modifiers, publishes mouse buttons by index and
as virtual keys, preserves signed client coordinates, and releases held state
on focus loss. Character and IME messages remain on the text/UI path.

This is an event backend. It does not opt into `ProvidesPhysicalKeyState()`:
window events cannot discover keys which were already held when focus returned.
`PhysicalKeyState` uses its foreground-aware OS fallback for these adapters.

## Physical polling and UI snapshots

`PhysicalKeyState::IsDown(key, input)` accepts a key in the same physical
namespace as `Key`. An opted-in device provider must declare its key space:

- `KeyCodeSpace::LayoutVirtualKey` is the default for existing device adapters
  which translate layout virtual keys to native scan codes.
- `KeyCodeSpace::PhysicalPosition` means the adapter already accepts physical
  positions, so the reader must not translate a second time.

A stale provider answers false. Without an opted-in provider the helper polls
only while this process owns the foreground window, using that window's layout
to translate the requested physical position.

Use `PhysicalKeyState::Update(snapshot)` for UI hotkeys which must remain live
while native gameplay devices are paused. `KeySnapshot` seeds held keys on
initial acquisition, foreground regain and stale-state recovery without firing
new press edges. Its state is unavailable while the source is unavailable or
stale. Gameplay availability remains a separate policy: use the client's
`IsLocalInputAvailable()` for gameplay hotkeys and explicit UI/editor policy for
controls which must work while a menu owns gameplay input.

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
focus/stale resynchronization and resource ownership. Windows additionally
covers actual window messages, modifiers, mouse masks, physical key spaces and
the physical-key mapping suite. The same modules run in `FrameworkTests`.

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
