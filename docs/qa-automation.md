# QA automation core

The first adapter is KCDC. Its [test guide](../code/projects/kcdc/docs/qa-automation.md)
has the build commands, one-client startup check, two-client movement check,
manual commands, evidence layout, and coverage limits.

`Framework::Automation::Session` in `code/framework/src/automation/` owns a
loopback HTTP listener, copied state, a command queue, retry results, an event
ring, an asynchronous JSONL journal, and a contact lease. It has no native game
or renderer dependency. Adapters call `AdvanceTick`, drain `TakeCommands`,
perform work on their owning thread, call `Complete`, and publish observations.
`Framework::Input::VirtualInput` supplies process-local key and cursor state.
Python clients and process supervision live in `scripts/qa/session.py`.

## Opt in

`Options::FromEnvironment()` enables QA when `FW_QA_DIR` is nonempty. It refuses
release builds. The runner supplies these variables separately for every role:

| Variable | Meaning |
| --- | --- |
| `FW_QA_DIR` | Absolute, existing instance directory strictly inside the workspace |
| `FW_QA_WORKSPACE` | Absolute, existing checkout directory; file policy boundary |
| `FW_QA_TOKEN` | 32–128 character bearer token |
| `FW_QA_ROLE` | Role name, such as `client-a` or `server` |

Use fresh directories. `Start` refuses an existing `endpoint.json`. Discovery
contains the protocol version, role, and ephemeral loopback URL. The token is
kept separately by the runner. Request IDs belong to one process session; a
restart requires fresh state and fresh IDs.

## Protocol 1

Every route requires `Authorization: Bearer <token>`.

| Route | Result |
| --- | --- |
| `GET /status` | Role, tick, capabilities, copied snapshot, last sequence, evidence-loss flag |
| `GET /events?after=<sequence>` | Events after the cursor, new cursor, gap and evidence-loss flags |
| `POST /command` | Queue a named operation; returns its request ID and state |
| `GET /operation?id=<request_id>` | Queued, completed, or failed result |

```json
{
  "request_id": "caller-generated-unique-id",
  "operation": "input",
  "arguments": { "actions": ["moveforward"], "ticks": 30 }
}
```

An identical retry returns the existing operation, including its final result.
Reusing an ID with different content returns 409. Queue acknowledgment means
the operation has been accepted for execution. It says nothing about native
gameplay success. Commands are JSON objects capped at 64 KiB. A session keeps
512 ordinary operation IDs and queues at most 32 ordinary commands; quit has a
small reserved budget. The owner drains at most four commands per update.

Events contain `sequence`, `source`, `tick`, `monotonic_ms`, `name`, and copied
`payload`. The ring holds 4096 events. An older cursor reports `gap`; the
separate journal retains events unless its bounded writer queue overflows or
the file fails. Both conditions set `evidence_lost`. The Python client refuses
to treat incomplete evidence as success.

Adapters provide their own operations, readiness predicates, and capability
flags. KCDC provides input, release, action discovery, capture, and quit; its
server provides observation and quit. Rendering, native input, profile
redirection, and audio suppression belong to the game adapter, outside this
portable core.
