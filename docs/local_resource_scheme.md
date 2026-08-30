# Serving local content to a web view (`fw://`)

A CEF view needs its HTML, CSS, JavaScript and art from somewhere. The framework
serves them on its own URL scheme, `fw://`, registered as a standard, secure,
CORS- and fetch-enabled scheme in every process of the CEF instance.

```cpp
webManager->RegisterResourceDirectory("my-ui", moduleDirectory / "files" / "ui");
view = webManager->CreateView(Framework::GUI::Manager::ResourceURL("my-ui", "index.html"), 0, 0);
```

That is the whole client-side setup. `fw://my-ui/index.html` now loads, and every
relative URL inside the page resolves against the same root.

## Why not `file://`, and why not a fake `http://` host

`file://` gives a page the opaque origin `null`. Anything gated on an origin then
fails: `fetch`, ES modules, `localStorage`, and the framework's own per-view
origin lock, which has nothing to compare against.

A fake host on the real `http` scheme — `http://my-ui/index.html` — fixes the
origin but buys three problems. The host is not reserved, so it is only a name
nobody happens to be using. The origin is not secure, so the page loses
`crypto.subtle`, service workers and anything else behind a secure context, and
is treated as mixed content beside https. And a missing file cannot be answered
properly: a scheme handler factory that returns null hands the request back to
Chromium's real HTTP stack, which tries to resolve the host over DNS and shows a
network error instead of a 404.

`fw://` is a registered scheme with a real, secure origin, and every request for
it reaches the framework, including the ones that miss.

## Hosts and providers

The scheme name is fixed framework-wide. `cef_subprocess.exe` is one shared
binary and every process in a CEF instance has to register the identical set of
custom schemes, so a per-project scheme would mean a per-project subprocess.
Projects claim **hosts** under the one scheme instead.

A host is backed by a `ResourceProvider`:

| provider | serves |
| --- | --- |
| `DirectoryProvider` | one directory on disk |
| `MemoryProvider` | bytes held in the process |
| your own | anything else — an archive, a download cache, a generated page |

```cpp
auto pages = std::make_shared<Framework::GUI::Resources::MemoryProvider>("pages");
pages->Set("status.html", BuildStatusPage(), "text/html");
webManager->RegisterResourceRoot("status", pages);
```

A provider implements one method:

```cpp
std::unique_ptr<ResourceStream> Open(const std::string &path, ResourceStat &stat) const;
```

`path` arrives percent-decoded, without its leading slash, without a query or
fragment, with `index.html` supplied for a directory-shaped request, and already
rejected for traversal. `stat` carries the body length, an optional MIME override
and an `immutable` flag for content-hashed assets.

`Open` runs on a CEF file thread, so it may block on real disk access. It must be
safe to call concurrently, because several responses from one host can be in
flight at once.

## What the handler does for you

- Reads never touch the CEF IO thread. `Open`, `Skip` and `Read` all defer to
  `TID_FILE_BACKGROUND` and return immediately, so a cold cache or a slow drive
  cannot stall resource dispatch for every other browser in the process.
- A missing file is a real `404`, a write method is a real `405`, and a path that
  tries to leave the root is a real `403` — each with a body the network panel
  can show.
- Text types are tagged `charset=utf-8`. Without it Chromium guesses from the
  locale and non-ASCII content renders as mojibake.
- Responses carry `x-content-type-options: nosniff`. Cross-origin reads are
  allowed **only for pages already on `fw://`**, so one host can fetch another
  while a remote page loaded into a view — `Web.createView` accepts any URL —
  gets no `access-control-allow-origin` at all and cannot read the asset cache.
  A preflight `OPTIONS` is answered `204` with the methods and requested headers
  for those origins.
- Loose files are `no-cache, must-revalidate`, so editing one and reloading shows
  the edit. A subtree marked with `DirectoryProvider::MarkImmutable` — a bundler's
  content-hashed output — gets a one-year immutable cache instead.

## Path safety

`NormalizeResourcePath` decides what a provider is allowed to see, on the URL,
before any provider maps it onto a filesystem. It rejects a `.` or `..` segment,
a percent-escape that would decode to a separator or a NUL, a malformed escape, a
backslash, and a colon (a drive letter or an NTFS alternate stream). It has no
CEF dependency precisely so the unit tests can drive it directly; see
`code/tests/modules/gui_resources_ut.h`.

`DirectoryProvider` then repeats the check against the resolved path, because the
two catch different things: the URL check stops an encoded `..`, and the canonical
check stops a symlink inside the root from pointing out of it.

## Scripted resources

The client integration serves every synced resource on this scheme:
`fw://resources/<resource>/<file>` is the per-server asset cache, so a resource
can ship its own pages and reference them directly.

```js
const view = Web.createView("fw://resources/my-ui/index.html", { width: 0, height: 0 });
```

This origin was `http://resources/...` before the scheme existed. Update any page
URL, image base or dev-server prefix that still names the old one; nothing else
about the layout changed, so the paths after the host are the same.
