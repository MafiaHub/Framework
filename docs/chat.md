# Chat

The framework ships a complete, **optional** chat feature in three independent
layers, so a mod (and a resource author) can take as much or as little as it
wants:

1. **Networking** — a `ChatMessage` RPC and the send/receive plumbing.
2. **Scripting** — a global `Chat` object on both client and server, plus a
   reserved `chatMessage` client event.
3. **Overlay** — a built-in in-game chat window (`ChatBox`) the client `Instance`
   owns but never forces on screen.

It is the framework's answer to MTA:SA's `outputChatBox` / `showChat` /
`onClientChatMessage` and FiveM's `chat` resource: the transport and the script
idioms are provided, and the UI is replaceable. Nothing renders and no line is
relayed unless something asks for it — a headless server, a HUD-less client, or
a resource shipping its own CEF chat all work by using fewer layers.

> **Framework vs host mod.** This document marks framework API plainly. A few
> pieces are supplied by the *host mod* (e.g. M2O) rather than the framework —
> the `playerChat` / `playerCommand` script events, the `Chat.setDefaultRelay`
> toggle, and the key that opens the overlay. Those are called out where they
> appear; a different mod may name or wire them differently.

---

## 1. Networking

All chat crosses the wire as one RPC, `Framework::Networking::RPC::ChatMessage`:

```cpp
struct ChatMessage {
    std::string text;     // message body
    std::string author;   // sender name; empty = system/notice line
    uint32_t    color = 0; // packed 0xRRGGBBAA for the body; 0 = client theme default
};
```

The payload is **structured, not a pre-formatted string** — the receiver
reconstructs the display, so a server can drive per-line presentation (author,
color) and grow the struct without reformatting on the client.

**Outgoing (client → server).** `Instance::SendChatMessage(text)` broadcasts a
`ChatMessage` carrying only `text`; the server resolves the real sender from the
connection and ignores any client-supplied `author`/`color`.

**Receive + dispatch (server).** The framework registers the RPC and calls
`HandleIncomingChat`, which splits plain lines from `/`-commands and hands them
to two virtuals a mod overrides:

```cpp
virtual void OnChatMessage(uint64_t senderNetworkId, const std::string &text);
virtual void OnChatCommand(uint64_t senderNetworkId, const std::string &text,
                           const std::string &command,
                           const std::vector<std::string> &args);
```

Lines beginning with `/` go to `OnChatCommand` (command + whitespace-split
args); everything else to `OnChatMessage`. The framework defaults are no-ops —
the mod decides what to broadcast back and which script events to raise.

**Outgoing (server → client).** Send with the server `Chat` builtin (below) or
the RPC directly.

**Receive (client).** The framework calls
`OnChatMessageReceived(const RPC::ChatMessage&)` (a virtual you may override),
emits the reserved `chatMessage` script event, and feeds the built-in overlay
when it is visible.

---

## 2. Scripting

### Client — global `Chat`

Client-side only. Available to any client resource with no import.

```ts
Chat.send(text: string): void          // send a line to the server
Chat.setUIVisible(visible: boolean): void
Chat.isUIVisible(): boolean            // built-in overlay visible?
Chat.open(): void                      // open the overlay input box
Chat.close(): void
Chat.isOpen(): boolean                 // overlay input box focused?
```

- **`send`** broadcasts a `ChatMessage`. A `/`-prefixed line becomes an
  `OnChatCommand` on the server, exactly like typing it.
- **`setUIVisible(false)`** hides the built-in overlay so a resource can render
  its own chat instead; `send` and the `chatMessage` event keep working. Reset
  to `true` at the start of every session.
- **`open` / `close` / `isOpen`** drive the overlay's text input (for binding
  your own open key, or scripting focus). No-ops when no overlay is rendered.

**Incoming lines** arrive as the reserved **`chatMessage`** event:

```js
Events.on("chatMessage", (msg) => {
    // msg = { author: string, text: string, color: number /* 0xRRGGBBAA, 0 = default */ }
});
```

**Outgoing lines** are offered to the reserved **`chatSend`** event *before* they
reach the server — the client-side counterpart of MTA:SA's cancellable
`onClientChatMessage`. The handler receives the submitted line and returns
`false` to keep it client-side:

```js
// Intercept a client-only command so it never reaches the server
Events.on("chatSend", (text) => {
    if (text.startsWith("/waypoint ")) {
        setWaypoint(text.slice(10));
        return false;
    }
});

Events.on("chatSend", (text) => !text.includes("badword"));
```

Semantics:

- Fires for **every line the player submits** in the overlay, `/`-commands
  included — that is what makes client-side commands possible.
- **`Chat.send` does not fire it.** It is the raw transport, so a handler can
  block a line and send its own without re-entering itself. That is also how you
  rewrite a message: `return false`, then `Chat.send(edited)`.
- Dispatch is **synchronous**, unlike the rest of `Events`: the client needs
  the verdict before it sends. An `async` handler's promise is not awaited, so it
  **cannot block** the line — decide synchronously.
- Every handler runs even after one returns `false`, and only a **literal
  `false`** blocks (a handler that forgets to return a value can't silently eat
  chat).
- A throwing handler is logged and skipped; it does not block the line.

### Server — global `Chat`

Reusable across mods; targets players through any replicated entity handle.

```ts
Chat.sendToAll(text: string, opts?: { author?: string, color?: number }): void
Chat.sendToPlayer(player: Entity, text: string,
                  opts?: { author?: string, color?: number }): void
```

- `author` omitted → a system/notice line. `color` is a packed `0xRRGGBBAA`
  number; `0` (or omitted) uses the client theme default.
- `sendToPlayer` delivers to the entity's owning connection.

**Receiving chat on the server is mod-defined.** The framework gives you
`OnChatMessage` / `OnChatCommand` (§1); a mod bridges them to script events. In
M2O those are the reserved **`playerChat`** and **`playerCommand`** events:

```js
Events.on("playerChat", (player, text) => { /* ... */ });
Events.on("playerCommand", (player, command, args) => { /* ... */ });
```

---

## 3. Built-in overlay (`ChatBox`)

A translucent top-left chat window with a fading backlog and a single-line
input (Up/Down history, Enter sends, Esc cancels). It lives in the framework as
`Framework::Integrations::Client::UI::ChatBox` and is **owned by the client
`Instance`** (`Instance::GetChatBox()`), alongside the renderer and web manager.

It is **inert until a mod renders it** — the framework instantiates it, wires
its submit path to `SendChatMessage`, feeds it received lines, and tracks its
session/visibility, but draws nothing. A mod opts in by rendering it and
supplying the game-specific input glue:

```cpp
// In the mod's ImGui pass:
if (GetChatBox().IsVisible()) {
    GetChatBox().Render();
}

// Open key (the mod owns the key + the "may I open?" gate):
if (canOpenChat && (GetAsyncKeyState('T') & 1)) {
    GetChatBox().OpenInput();
}

// Feed the mod's control lock so movement stops while typing:
const bool typing = GetChatBox().IsInputActive();
```

Everything the scripting `Chat` verbs touch is resolved through
`CoreModules::GetClientInstance()->GetChatBox()`, so there is no registration
handshake: render it to use it, don't to drop it. A mod with its own chat UI
(e.g. hogwarts) simply never renders it.

---

## Idiomatic integration

### Default overlay (do nothing)

If the host mod renders `GetChatBox()` (M2O does), chat works out of the box:
lines the server relays appear, and the open key brings up the input. A resource
needs no code. Send helper lines from anywhere with the server `Chat` builtin:

```js
Chat.sendToAll("Round starting in 10s…", { color: 0x9CA8FFFF });
Chat.sendToPlayer(player, "Welcome!", { author: "Server" });
```

### Bring your own UI (CEF)

Hide the overlay and render the `chatMessage` stream however you like, while
keeping the same networking:

```js
// client resource
Chat.setUIVisible(false);                              // hide the built-in overlay
const view = Web.create("chat.html");
Events.on("chatMessage", (msg) => view.emit("chat:add", msg)); // {author,text,color}
Web.on(view, "chat:send", (text) => Chat.send(text));     // input box → server
```

The CEF view's keyboard focus already participates in the client's input gate
(`Web.focusView`), so movement/keybinds pause while the box is
focused, exactly as with the built-in overlay.

On the server, format lines yourself from `playerChat` and — if the mod exposes
it — turn off the mod's default relay so only your formatting goes out:

```js
// server resource (M2O: Chat.setDefaultRelay is a mod-provided toggle)
Chat.setDefaultRelay(false);
Events.on("playerChat", (player, text) =>
    Chat.sendToAll(text, { author: player.name, color: 0x9CA8FFFF }));
```

### Headless / no overlay

A server never touches the overlay; `Chat.sendToAll` / `sendToPlayer` and the
`playerChat` / `playerCommand` events are the whole API. On a client that never
renders the overlay, `Chat.send` and the `chatMessage` event still work — the
overlay verbs (`open`/`isUIVisible`) just no-op.

---

## Notes and limits

- **Colors** are packed `0xRRGGBBAA`. A zero alpha byte is treated as opaque, so
  a bare `0xRRGGBB` still shows. `0` means "use the theme default".
- **Author trust.** The server ignores `author`/`color` on inbound client lines;
  only server-sent lines carry them. Set the author server-side.
- **One overlay** per client — the `Instance` owns a single `ChatBox`. Multiple
  channels/tabs are a bring-your-own-UI concern.
- **Netcode.** `ChatMessage` is a shared RPC: client and server must ship
  together (a change here is a MAJOR version bump).
- See also `docs/scripting_keybindings.md` (binding your own open key) and the
  M2O demo resource for a full server + client example.
