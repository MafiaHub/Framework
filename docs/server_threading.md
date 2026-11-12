# Server threading

How a server uses the host's cores, and how game code takes part.

## The rule

There is one world: the replicated entities, the connections, everything a
mod's `Instance` holds. One thread has it at a time. The **tick thread** has
it for the length of a tick; the **script thread** has it whenever it runs
anything. Every other thread either works on data it was handed, or runs
while the tick thread waits for it.

That rule is what lets the rest be simple: there are no locks on entities,
and code that runs in `PostUpdate`, an RPC handler or a script binding needs
none.

## The threads

| Thread | What it does | Sized by |
| --- | --- | --- |
| Tick | Packets, RPC handlers, replication, `PostUpdate`, completions | one |
| Script | Every resource's runtime, and what the tick thread posts to scripts | one (`script-thread`) |
| Network (MafiaNet) | Sockets, reliability, the voice relay | one |
| Replication workers | Viewer interest, construct/destroy lists, the serialize pass, a game's own per-tick loops | `replication-threads` |
| Background workers | Blocking work handed off the tick | `background-threads` |
| Script platform | V8 collection and compilation for every resource's isolate | cores |
| Web workers | Downloads, the info endpoint, mod routes | connection slots |

`Utils::ThreadBudget` shares the cores out. Left alone it gives replication
every core but two (tick and network), up to 12, where the pass stops being
bound by its workers; one background worker per four cores, between 1 and 4;
and half the cores to the script platform, between 4 and 8.

`server.json` (or the flags of the same names) pins them:

```json
{
    "replication-threads": -1,
    "background-threads": -1
}
```

`-1` leaves the count to the machine. `0` keeps that work on the tick thread,
which is the setting for a one- or two-core host and for bisecting a problem.
The server logs the layout it chose at start.

## Loops inside a tick: `ForkJoinPool`

For work that has to be finished before the tick moves on and splits into
independent iterations:

```cpp
void MyServer::PostUpdate() {
    auto &npcs = _npcs;
    GetTickWorkers()->Run(static_cast<uint32_t>(npcs.size()), [&](uint32_t i, uint32_t lane) {
        npcs[i].path = FindPath(npcs[i], _scratch[lane]);
    });
}
```

`Run` returns when every iteration has run; the tick thread is one of the
lanes. Nothing else runs meanwhile, so iterations may read anything the tick
thread owns and write only their own result. `lane` is below `Lanes()` and
indexes per-thread scratch without a lock. A loop started from inside an
iteration runs on the thread that started it, under the same lane.

Replication uses these same workers, at a different point of the tick, so
the two never compete.

## Work that blocks: `TaskScheduler`

For work that may take longer than a tick (disk, a database, an HTTP call):

```cpp
Framework::CoreModules::GetTaskScheduler()->Compute(
    [path] {
        return LoadAccount(path); // a background worker: touch nothing the tick thread owns
    },
    [this, guid](Account account) {
        ApplyAccount(guid, std::move(account)); // the tick thread, at the top of a later tick
    });
```

- `RunInBackground(work)` and `RunInBackground(work, done)` run `work` on a
  background worker; `done` runs on the tick thread. Copy what `work` needs
  into it: the objects it was copied from may change or go before it runs.
- `PostToOwner(task)` runs `task` on the tick thread, from any thread.
- The tick drains completions once, before the packet pump. A task posted
  while it drains waits for the next tick.
- A task that throws is logged and dropped, and its completion is skipped.
- At shutdown, queued background work is finished; completions that no tick
  is left to run are dropped. A completion must not be the only place a
  resource is released.

The scheduler is Asio underneath: an `asio::thread_pool` for the workers and
an `asio::io_context` the tick thread polls. Asio stays out of the headers a
project includes.

The FTL job system (`jobs/`) is a different tool and is not used here. Its
tasks run on 512 KB fiber stacks, it drops queued tasks when it is destroyed,
and it switches the thread that owns it onto a fiber, which on a server is
the thread running V8.

## HTTP routes that read the world

A route registered with `RegisterRequest` runs on a web worker, several at
once. It may read only what it guards itself.

A route that reads or changes the world is registered with
`RegisterTickThreadRequest`. The web worker waits while the tick thread runs
the handler:

```cpp
GetWebserver()->RegisterTickThreadRequest("/players", [](const httplib::Request &, httplib::Response &res) {
    res.set_content(std::to_string(Framework::CoreModules::GetReplication()->GetConnectionCount()), "text/plain");
});
```

It answers 503 when the tick thread has not run it within five seconds, and
a handler given up on is never run afterwards; 500 when the handler throws.

## What the framework itself moved

- **Replication.** Interest is computed per viewer on the workers, and with
  it now what each viewer gains and loses against what it already holds, so
  the serial part of the pass only hands two lists to ReplicaManager3. A
  list is used only while nothing was destroyed since it was made; otherwise
  the pass decides again, as before. `PreparedDiffsLastPass()` counts them.
- **Publishing client files.** Packaging a resource, building its asset paks
  and hashing their chunk lists happen on a background worker when a
  resource is hot-reloaded, and on all replication workers at once at boot.
  New files are written beside the ones being served and take their place in
  a tick, so what the server has announced is always what it serves.
- **Script platform.** Node's platform pool is shared by every resource's
  isolate; it now grows with the machine instead of staying at four.

## Scripts on their own thread

With `"script-thread": true`, the default, every runtime is created, run and
freed on the script thread. A script that computes for 300 ms holds up the
other scripts and nothing else: the tick asks for the world and takes it at
the script's next **safe point**, replicates, and gives it back.

A safe point is between two tasks of the script thread, or between two
instructions of a running script (V8 interrupts it at a loop edge or a
function call; the script keeps its place and carries on afterwards). So:

- A script's native call is never interrupted. It runs with the world from
  start to end, exactly as it did inside the tick.
- Between two statements of a script, a tick may have run. An entity looked
  up on one line may be gone on the next, as it always could be after an
  `await`. Bindings look entities up by id on every call, so this is a
  stale answer, never a dangling one.
- No safe point is taken while native code that the script called is on the
  stack and has called back into script (an event handler run from
  `Events.emit`, a state-bag listener, an export of another resource): that
  native code is in the middle of its work. The tick waits for that call to
  return and keeps asking meanwhile. V8 does not report the return, so it is
  read off the stack: the call is over once the script is no deeper than
  where it made it. A script that went into deeper calls of its own right
  after is taken for still being inside it until it comes back up.
- A script blocked inside a native call that takes long on its own
  (`fs.readFileSync` of a large file, `execSync`) cannot be interrupted
  either. Use the asynchronous forms.

`"script-thread": false` runs scripts inside the tick, as before.

### What changes for code on the tick thread

Code on the tick thread is `PostUpdate`, RPC and packet handlers, and the
`On*` overrides the tick calls. It may still do what it always did: open an
`EngineScope`, emit an event, start or stop a resource. That now first puts
the script thread out of whatever it is running and keeps it out until the
scope closes, which **waits for the running script**. It is correct and it
gives up, for that tick, what the script thread is for.

Hand the work over instead:

```cpp
void MyServer::OnPlayerConnect(const PlayerConnectionData &data) {
    SpawnAvatar(data); // the world: stays here
    PostToScripts([this, guid = data.guid] {
        // The script thread, with the world: an EngineScope, an event, anything tick code may do.
        EmitPlayerJoined(guid);
    });
}
```

- `PostToScripts(task)` queues `task` for the script thread, which looks at
  its queue about every 2 ms between the scripts it runs. That is an
  interval, not a deadline: a task waits behind the tasks queued before it
  and behind a script that is in the middle of a long callback. With scripts
  inside the tick it runs at once.
- `RunOnScriptThread(task)` waits for it. From the tick thread the world may
  have changed when it returns.
- A handler's answer can no longer be read on the line after the emit. Have
  the task act on it, or post the outcome back with
  `GetScheduler()->PostToOwner`.

The framework's own events already go this way: client events
(`Events.onClient`), `playerConnecting` and the admission that follows,
voice events, console commands and resource hot reload.

Two things are not supported and deadlock or crash:

- Entering a runtime from the tick thread with V8's own scopes (`v8::Locker`
  and a `Function::Call`) instead of `EngineScope`. The script thread may be
  stopped in the middle of that runtime.
- Calling into script from native code while holding a pointer the tick may
  free, without `Scripting::NoSafePoint` around the call. The framework's
  entries (`EngineScope`, calls between runtimes) hold safe points off by
  themselves; a binding that calls a JS function directly and goes on using
  an entity pointer afterwards needs the guard.

### What it costs

- An event raised by the tick reaches its handlers about 2 ms later when the
  script thread is idle, and later than that behind a long callback.
- The resource monitor charges a script for the time it stood at a safe
  point, so a script that was interrupted reads slower than it ran.
- `tick.scriptsMs` in the slow-tick warning is now the time the tick waited
  for scripts to reach a safe point.

## What still runs on the tick thread

Packet handling, RPC dispatch and inbound transforms. Each resource has its
own isolate, and all of them share the script thread, as in FiveM and
MTA:SA: one slow script still delays the other scripts. Long script work
belongs in a `worker_threads` worker.

## Measuring

`FrameworkReplicationBench` reports the replication half of a tick.
`FW_BENCH_WORKERS=0,3,12` picks the worker counts:

```
FW_BENCH_WORKERS=0,3,-1 build/bin/FrameworkReplicationBench 250 500 1000
```

On a live server the slow-tick warning says where a tick's time went, and
Tracy shows `Server::TickThreadTasks`, `Replication::CollectInterest` and
the pass.
