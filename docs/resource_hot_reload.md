# Resource Hot-Reload

The scripting layer can reload a JavaScript resource from disk without
restarting the server, and propagate that reload to connected clients. This is
a development aid: edit a resource's `.js`, and the running resource picks up
the change.

## Enabling

Hot-reload triggers come in two forms:

- **Console commands** (always available), registered on the server `Instance`:
  - `ensure <resource>` — start the resource if stopped, reload it if running.
    The FiveM-style canonical reload verb.
  - `refresh <resource>` — reload a running resource (re-parsing its manifest);
    leaves a stopped resource stopped.
  - `refreshall` — re-scan the resources directory and reload everything that
    was running.
- **Automatic file watcher** (opt-in): set `InstanceOptions.developmentMode =
  true` on the server. The watcher polls running resources' files and reloads
  any that change. It is **off by default** — leave it off in production.

```cpp
Framework::Integrations::Server::InstanceOptions opts;
opts.developmentMode = true; // enable the file watcher
```

The watcher interval defaults to 1s (`ResourceManagerConfig::fileWatchIntervalMs`)
and skips `node_modules`/`.git`.

## What a reload does

`ResourceManager::RefreshResource(name)` (and `RefreshAll`):

1. **Stops** the resource (and any dependents that cascade), firing
   `resourceStop` and running `Events::CleanupResource` so the resource's
   framework event listeners are removed.
2. **Cancels the resource's timers.** `Engine::ClearResourceTimers` cancels any
   `setTimeout`/`setInterval` the resource created, so a shared runtime doesn't
   keep firing — or duplicate — them across reloads.
3. **Evicts the resource's cached modules.** `Engine::EvictModulesUnderPath`
   removes the resource's entries from the module cache (Node `require.cache`
   on the server, the V8 module cache on the client), so re-execution re-reads
   the edited files instead of returning stale exports.
4. **Re-parses `package.json`** from disk (manifest edits — entry points,
   dependencies — take effect) and rebuilds the dependency graph.
5. **Restarts** the resource and the dependents that were stopped.

`RefreshAll` additionally rescans the resources directory and registers
newly-added resource directories (left stopped — `start` them explicitly).

## Client propagation

Whenever a client resource **starts at runtime** — a reload restart, an error
auto-restart, or a newly started resource — the server notifies connected
clients so they re-sync and apply it in place:

1. Any `StartResource` after the initial boot fires
   `ResourceManager::SetOnResourceStarted`; the server `Instance` reacts (boot
   starts are skipped — clients get the full list on connect).
2. The server rebuilds the asset streamer's upload list (`ClearUploads` +
   `InitAssetStreamer`). This is required: MafiaNet's `DirectoryDeltaTransfer`
   compares the file hashes captured when files were added, so without
   rebuilding, an edited file looks up-to-date and is never re-sent.
3. The server broadcasts a `ResourceRefresh` RPC (affected resource
   names/versions), for resources with a client entry point.
4. The client re-runs a **targeted** delta download (only changed files
   transfer; unlike the connect-time download it does not stop all resources),
   then for each flagged resource: reloads it if running, or — if the client
   doesn't know it yet (newly added on the server) — discovers it from the
   just-synced cache and starts it.

Clients that haven't finished connecting ignore `ResourceRefresh` — their
initial asset sync already fetches the current files.

## Limitations

- **CommonJS only.** Module-cache eviction covers the framework's CJS load path.
  Resources loaded via dynamic ESM `import()` are not in `require.cache` and
  won't be re-read on reload.
- **Raw `EventEmitter` listeners leak.** Only framework event listeners
  (`on(...)`) and engine timers are cleaned up. Listeners a resource adds to its
  own emitters (or `process.on`) must be removed in a `resourceStop` handler.
- **`require('timers')` bypasses timer tracking.** Timer cleanup wraps the
  global `setTimeout`/`setInterval`; code importing the `timers` module
  directly is not tracked.

## Design notes (vs FiveM / MTASA)

FiveM and MTASA run **each resource in its own script runtime** (Lua state / V8
isolate / Mono domain). Reloading destroys and recreates that runtime, so
modules, timers, and event listeners are freed automatically — there is no
cache to evict or timer to cancel.

This framework runs **one shared Node runtime** for all resources. Everything in
the reload sequence above (module eviction, timer cancellation, listener
cleanup) exists to compensate for that shared runtime. A consequence worth
knowing: a plain stop+start does **not** reload code here — the entry point
re-executes against stale module caches. `RestartResource` therefore evicts the
resource's modules between stop and start, so "restart" reloads code the way it
does in a per-resource-runtime engine.

The robust long-term direction, if reload correctness becomes a recurring
concern, is **per-resource isolation** (a dedicated V8 context per resource):
reload would drop the context and rebuild it, eliminating the module-eviction,
timer-leak, and listener-leak classes at the source — the same property FiveM
and MTASA rely on.

Where this framework is already ahead of FiveM: reloading a dependency restarts
the dependents that cascaded down (FiveM leaves them stopped — see its
`#TODO: restarting behavior of stopped dependencies at runtime`).

## Versioning

Client propagation is a netcode change (new RPC, both client and server) — a
**MAJOR** change requiring matching client and server builds. The server-only
pieces (eviction, `refresh` commands, watcher, timer cleanup) do not affect the
wire protocol.
