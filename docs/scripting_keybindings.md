# Client Keybindings (`Key`)

The client scripting layer lets a resource bind physical keys to handlers via
the `Key` API. It is the framework's answer to MTA:SA's `bindKey` and
FiveM's `RegisterKeyMapping`: a resource asks for a key, and a callback fires
when that key goes down and/or up.

`Key` is **client-side only** — it does not exist on the server. Put
your `Key.bind` calls in a resource's client script.

## API

```ts
Key.bind(key, state, handler)   // state: "down" | "up" | "both"
Key.bind(key, handler)           // state defaults to "down"
Key.unbind(key, state?, handler?)
Key.isDown(key) -> boolean
```

- **`bind(key, state, handler)`** — registers `handler`. It is called as
  `handler(key, state)`, where `state` is the edge that actually fired
  (`"down"` or `"up"`). `state === "both"` fires on both edges. Returns `true`;
  throws on an unknown key name or an invalid state. Passing the handler as the
  second argument (`bind(key, handler)`) defaults the state to `"down"`.
- **`unbind(key, state?, handler?)`** — removes binds for `key`. With no
  filters it removes every handler on that key. Pass `state` and/or the exact
  `handler` function to narrow what is removed. Returns `true` if anything was
  removed.
- **`isDown(key)`** — the live physical state of a key, for polling inside your
  own loop. Returns `false` whenever binds are suppressed (see *When binds
  fire* below).

## Examples

```js
// Toggle a HUD panel on F6:
Key.bind("f6", "down", () => toggleHud());

// Hold-to-aim: one handler, both edges. `state` tells you which edge.
Key.bind("b", "both", (key, state) => {
    setAiming(state === "down");
});

// Fire-and-forget on key-down (state omitted):
Key.bind("h", () => honk());

// Poll a modifier from inside another handler:
Key.bind("e", "down", () => {
    if (Key.isDown("lshift")) openContextMenu();
    else interact();
});

// Remove a specific bind later:
const onJump = () => jump();
Key.bind("space", "down", onJump);
// ...
Key.unbind("space", "down", onJump);
```

## Key names

Names are **case-insensitive**. The recognised set:

| Group      | Names |
|------------|-------|
| Letters    | `a`–`z` |
| Digits     | `0`–`9` |
| Function   | `f1`–`f12` |
| Arrows     | `up` `down` `left` `right` |
| Modifiers  | `shift` `lshift` `rshift`, `ctrl`/`control` `lctrl` `rctrl`, `alt` `lalt` `ralt` |
| Editing    | `space` `enter`/`return` `escape`/`esc` `tab` `backspace` `insert` `delete` `home` `end` `pageup` `pagedown` `capslock` |
| Numpad     | `numpad0`–`numpad9` (aliases `num0`–`num9`) |
| Mouse      | `mouse1` (left) `mouse2` (right) `mouse3` (middle) `mouse4` `mouse5` |

Binding an unrecognised name throws, so a typo fails loudly rather than
silently never firing.

## When binds fire

Handlers fire only when the player is actually in control of the game:

- the client is in an active session,
- no UI is capturing input — chat box open, a menu open, or a focused web view
  (`Web.focusView`), and
- the game window is in the foreground.

While any of those hold, key edges are **swallowed** — a key pressed with the
chat box open does not fire a bind when the chat closes, and `isDown` returns
`false`. This keeps typing in a text field from triggering gameplay actions.

Each host game supplies this gate, so the exact "UI is open" conditions are
mod-specific, but the rule of thumb holds everywhere: binds fire only when the
player could otherwise be driving/walking.

## Lifecycle

Binds are **resource-owned**. When a resource stops (or is hot-reloaded), all of
its binds are removed automatically — you do not need to `unbind` them in a
`resourceStop` handler. Re-registering in the new `resourceStart` is enough.

## Notes and limits

- Binds are dispatched by polling once per frame, so this is edge detection on
  the game's frame rate — fine for gameplay actions, not for text entry (use a
  `Web` view for typed input).
- There is no user-facing rebinding UI yet: the key a resource asks for is the
  key it gets. A default-plus-rebind model (FiveM-style) may be added later.
- Server-driven binds (a server telling a specific client to bind a key) are
  not yet available.
