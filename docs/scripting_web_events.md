# Web View Browser Events (`browser*`)

A web view reports what the browser does to it — pages loading, navigation being
refused, the cursor changing, the user starting to type. Those arrive as reserved
events on the normal `Events` bus, keyed by view ID and carrying a payload
object.

Web views are **client-side only**. Put these handlers in a resource's client
script.

A resource's own pages are served at `fw://resources/<resource>/<file>`; see
[`local_resource_scheme.md`](local_resource_scheme.md).

```js
Events.on("browserDocumentReady", ({ viewId, url }) => {
    if (viewId !== myView) return;
    Web.emit(myView, "state:init", { health: 100 });
});
```

Everything `Events` offers applies: `once`, `off`, the unsubscribe function `on`
returns, async handlers, `listenerCount`.

## Two channels, deliberately different

| | source | API |
| --- | --- | --- |
| Browser events | CEF tells us what the view did | `Events.on("browser*")` |
| Page events | the page calls `callEvent(name, payload)` | `Web.on` / `Web.off` |

Page events stay on their own API precisely because they are page-authored and
untrusted: a page cannot fake `browserDocumentReady` by naming it in
`callEvent()`, and `Web.emit` cannot reach an `Events.on` handler. It is the same
split as `Events.on` versus `Events.onClient` on the server.

## Scoping

Browser events are delivered **only to the resource that owns the view**, so a
resource never observes another's pages, URLs or console output. `viewId` in the
payload tells your own views apart.

Events are queued as they happen and delivered on the next scripting tick, so a
handler never runs while CEF is mid-callback. That also means `browserCreated`
and `browserOriginChange` still reach a handler registered immediately after
`Web.createView` — the events are already queued, and you subscribe before the
queue drains.

## Events

Every payload carries `viewId` in addition to the fields below.

| Event | Payload |
| --- | --- |
| `browserCreated` | `{ url }` |
| `browserLoadingStart` | `{ url, isMainFrame }` |
| `browserDocumentReady` | `{ url }` |
| `browserLoadingFailed` | `{ url, description, errorCode, isMainFrame }` |
| `browserNavigate` | `{ url, isMainFrame, blocked }` |
| `browserPopup` | `{ url, openerUrl }` |
| `browserCursorChange` | `{ cursor, cursorType }` |
| `browserTooltip` | `{ text }` |
| `browserInputFocusChange` | `{ focused }` |
| `browserResourceBlocked` | `{ url, domain, reason }` |
| `browserConsoleMessage` | `{ message, source, line, severity }` |
| `browserOriginChange` | `{ origin, url }` |

- **`browserCreated`** — the browser behind the view exists. Fires once, before
  any navigation.
- **`browserLoadingStart`** — a frame started loading. Sub-frames fire this too;
  check `isMainFrame`.
- **`browserDocumentReady`** — the main frame finished loading. This is where you
  start talking to the page (`Web.emit`); before it, the page has no listeners
  yet.
- **`browserLoadingFailed`** — a load was aborted. `errorCode` is the CEF error
  code, `description` its text (e.g. `ERR_CONNECTION_REFUSED`).
- **`browserNavigate`** — the view was asked to navigate. `blocked` says whether
  the request was refused; a refused main-frame request leaves the page where it
  was.
- **`browserPopup`** — the page tried to open a new window or tab. Popups are
  always blocked (there is no OS window to host one); handle the URL yourself,
  e.g. by calling `Web.loadURL` or opening it in the system browser.
- **`browserCursorChange`** — the cursor shape the page wants. `cursor` is a
  CSS-style name (`"default"`, `"pointer"`, `"text"`, `"grabbing"`, …, or
  `"custom"` for shapes without one); `cursorType` is the raw CEF value. Only
  transitions fire.
- **`browserTooltip`** — the page wants to show a tooltip. Windowless rendering
  has no window to draw a native one, so nothing appears unless you draw it.
- **`browserInputFocusChange`** — `focused` is `true` while a form control inside
  the page holds focus. Use it to stop forwarding keys to the game while the user
  is typing into the view.
- **`browserResourceBlocked`** — the view refused something. `reason` is one of:
  - `"cross-origin"` — main-frame navigation away from the view's locked origin
  - `"invalid-url"` — a request URL that could not be parsed
  - `"host-filter"` — rejected by the host mod's navigation filter
  - `"foreign-event"` — a `callEvent()` from a frame outside the locked origin
- **`browserConsoleMessage`** — a page console call. `severity` is one of
  `"debug"`, `"info"`, `"warning"`, `"error"`, `"fatal"`. These are logged by the
  framework regardless; the event is for surfacing them in-game.
- **`browserOriginChange`** — the view's allowed origin changed, which happens on
  creation and on every `Web.loadURL` to a different origin. There is no global
  page whitelist: each view is locked to one origin, and that lock is what this
  event reports.

## Examples

```js
const view = Web.createView("fw://resources/my-ui/index.html", { width: 0, height: 0 });
const mine = (p) => p.viewId === view;

// Wait for the page before pushing state into it.
Events.on("browserDocumentReady", (p) => {
    if (!mine(p)) return;
    Web.emit(view, "state:init", { health: 100 });
});

// Don't let the game eat keystrokes while the user types in the page.
Events.on("browserInputFocusChange", (p) => {
    if (mine(p)) setGameInputEnabled(!p.focused);
});

// Surface load failures instead of staring at a blank view.
Events.on("browserLoadingFailed", ({ url, errorCode, description }) => {
    console.error(`UI failed to load ${url}: ${description} (${errorCode})`);
});

// Links that would open a new tab: send them somewhere useful.
Events.on("browserPopup", (p) => {
    if (mine(p)) Web.loadURL(view, p.url);
});

// One-shot, and drop the handler when you're done with it.
const stop = Events.on("browserConsoleMessage", ({ severity, message }) => {
    if (severity === "error") reportUiError(message);
});
// ...
stop();
```

## Native side

The same events are available to a host mod in C++ through
`Framework::GUI::View::SetOnViewEventCallback`, which takes a
`Framework::GUI::ViewEventData` (see `gui/view_events.h`). It is a single slot
per view: `Web` claims it on views created from script, so a mod should only set
it on views it created itself.
