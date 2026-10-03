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
Key.getLabel(key) -> string
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
- **`getLabel(key)`** — display text for a physical key in the player's current
  keyboard layout. Works without registering a binding, including while menus
  or web views own input. Throws on an unknown key name or a non-string argument.

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

Letters and digits name physical positions on a US keyboard. For example,
`Key.bind("w", ...)` follows the same key with English, Russian (Ц), or
French AZERTY (Z) active. Switching layouts does not change the binding.
Text typed into chat or web views still follows the active layout.

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

## Showing key prompts

Use the binding name for input and `Key.getLabel` for the text you show players:

```ts
const INTERACT_KEY = "y";

Key.bind(INTERACT_KEY, "down", () => {
    Events.emitServer("interact");
});

// Call when showing or refreshing the prompt. viewId is your existing Web view.
function refreshInteractionHint(viewId: number): void {
    Web.emit(viewId, "interaction:hint", {
        key: Key.getLabel(INTERACT_KEY),
        text: "Interact",
    });
}
```

The page receives `Y` on US QWERTY and `Z` on German QWERTZ. Both refer to
the same physical position, between T and U. Keep `"y"` in `bind`, `unbind`,
and `isDown`; the returned label is display text, not a binding identifier.

The label uses the current Windows layout on every call. Refresh it when
showing a prompt, and periodically while the prompt stays visible if players
may switch layouts. There is no layout-change event in this API. While the
game is in the background, the query uses the calling game thread's layout.

Printable labels are uppercase Unicode text and ignore held modifiers and
Caps Lock. Other keys have English labels such as `Enter`, `Left Shift`,
`Numpad 1`, and `Mouse 1`. If Windows cannot translate a printable key, the
query falls back to its uppercase canonical name. This does not detect the
physical keyboard's printed legends if they differ from the selected layout.

Lookup leaves text composition untouched. Windows may include a pending
dead-key accent in a printable label while the player is composing text;
refreshing the prompt after composition finishes returns the ordinary label.

## When binds fire

Handlers fire only when the player is actually in control of the game:

- the client is in an active session,
- no UI is capturing input — chat box open, a menu open, or a focused web view
  (`Web.focusView`), and
- the game window is in the foreground.

While any of those hold, key edges are **swallowed** — a key pressed with the
chat box open does not fire a bind when the chat closes, and `isDown` returns
`false`. This keeps typing in a text field from triggering gameplay actions.

That baseline is the framework's own (`Instance::IsLocalInputAvailable`), so it
holds in every mod. A host game overrides the predicate to add its input owners
— a control lock, a debug menu, a gameplay-ready flag — so the exact "UI is
open" conditions are mod-specific on top of it, but the rule of thumb holds
everywhere: binds fire only when the player could otherwise be driving/walking.

## Lifecycle

Binds are **resource-owned**. When a resource stops (or is hot-reloaded), all of
its binds are removed automatically — you do not need to `unbind` them in a
`resourceStop` handler. Re-registering in the new `resourceStart` is enough.

## Notes and limits

- Binds are dispatched by polling once per frame, so this is edge detection on
  the game's frame rate — fine for gameplay actions, not for text entry (use a
  `Web` view for typed input).
- Native `IInput` implementations interpret printable codes as US physical
  positions. Layout conversion belongs to the input backend; label lookup
  does not change bindings or gameplay input.
- There is no user-facing rebinding UI yet: the key a resource asks for is the
  key it gets. A default-plus-rebind model (FiveM-style) may be added later.
- Server-driven binds (a server telling a specific client to bind a key) are
  not yet available.
