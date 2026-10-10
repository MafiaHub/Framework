# Connection admission

A server script decides whether a player may join **before the connection
exists**. The request waits inside MafiaNet's session handshake: the player
holds no player slot, is not counted as online, and is sent nothing -- no
resource list, no download, no world, no body. This is the framework's
`playerConnecting` event, FiveM's `playerConnecting` with deferrals under
another name.

```js
Events.on("playerConnecting", async (connection) => {
    connection.update("Checking the whitelist...");
    const entry = await whitelist.lookup(connection.steamId);   // DB, HTTP, anything async
    if (!entry) {
        return connection.reject("You are not on the whitelist. Apply at example.org");
    }
    // Returning without rejecting lets the player in.
});
```

---

## 1. Where it sits

```
client                                        server
  | -- connect (transport, join password) ------>|  MafiaNet: a pending-session slot, not a player slot
  | -- ID_SESSION_CONFIG_REQUEST (identity) ---->|  interactive: surfaced to the framework
  |                                               |  playerConnecting ... handlers settle
  |<-- ID_SESSION_CONFIG_STATUS (update(), 0..n) -|  restarts the session timeout on both ends
  |<-- ID_SESSION_CONFIG (session config) --------|  AcceptSession: the connection now exists
  |    or ID_SESSION_CONFIG_REJECTED (reason) ----|  RejectSession: it never will
  | -- build challenge, resource list, download, ClientJoin: unchanged
```

The identity (`RPC::ClientIdentity`: nickname, ids, ticket) is the client's
MafiaNet **session payload**, carried in the connection request itself. The
server's session config (the replicated `server.json` subset) is the accept's
payload, so a client only learns the level and the mod config once it is let
in.

Everything after acceptance is the handshake the framework always had: build
token challenge, `ServerResources`, the asset download, `ClientJoin`, the spawn
barrier. None of it can start for a refused player, because a refused player
never has a connection to run it over.

---

## 2. Why MafiaNet

A connection used to take a player slot from its first packet: a peer that
stalled anywhere in the handshake held a slot, and it counted in the masterlist
and console as online. Deciding admission on top of such a connection only
moves the problem.

MafiaNet's interactive session handshake, extended for this
(`SetMaximumPendingSessions`, `SetSessionTimeout`, `SendSessionStatus`,
`ID_SESSION_CONFIG_STATUS`, `ID_SESSION_CONFIG_ABANDONED`), holds the request
in a **pending pool** beside the player slots:

- `maxPlayers` bounds connected players only. Waiting requests are not counted
  by `NumberOfConnections()`, so neither the masterlist nor the console shows
  them.
- The pool bounds how many requests may wait (`pendingConnections`, 32 by
  default) and how many one IP address may hold
  (`pendingConnectionsPerAddress`, 4). A flood of stalled handshakes fills the
  pool and is refused there; players keep their slots.
- A request may start while the server is full, which is what lets a script
  queue people. Letting one in still needs a free slot: a full server refuses it
  with "The server is full." -- and MafiaNet refuses an accept past
  `SetMaximumIncomingConnections` on its own as a backstop.

---

## 3. The decision

`playerConnecting` gets one `PendingConnection`. Handlers may return a Promise,
and the request waits for all of them:

| What happens | Result |
|---|---|
| Every handler returned, every Promise fulfilled, nobody called `reject` | admitted, if a player slot is free |
| No slot free | refused: "The server is full." |
| `connection.reject(reason?)` | refused with `reason` (or "The server refused the connection.") |
| A handler throws, or its Promise rejects | refused: "The server could not check your connection. Try again later." -- the error goes to the server log |
| Handlers not settled within `admissionTimeoutMs` (30 s) | refused: "The server did not answer in time. Try again later." |
| The player gives up, or the transport drops | `ID_SESSION_CONFIG_ABANDONED`; forgotten, `isPending()` turns false |
| The request carried no identity (another build) | refused: "Your client could not be identified. Update it and try again." |
| No `playerConnecting` handler at all | admitted at once |

The gate **fails closed**. Decisions are applied once per server tick, never
from inside the script call. `connection.update(message)` shows the player one
line and restarts both the gate's timeout and MafiaNet's session timeout, so a
queue that keeps its players informed is never timed out.

MafiaNet keeps a waiting request 15 s longer than the gate's timeout, so the
player always reads the gate's reason rather than a silent drop. The client
waits up to two minutes for an answer; it is always the server that decides.

---

## 4. `PendingConnection`

| Member | |
|---|---|
| `nickname` | the name the player asked to join under |
| `steamId`, `discordId`, `hardwareId` | as reported by the client; empty when absent |
| `epicId` | Epic account ID reported by the client; empty when absent |
| `ticket` | the `ticket` of the client's launch link, untouched |
| `ip` | remote address, no port |
| `reject(reason?)` | refuse; reason up to 512 bytes |
| `update(message)` | status line up to 256 bytes; restarts the timeout |
| `isPending()` | false once decided or the player left |

**Nothing in it is verified.** Every identifier is what the client said about
itself; `steamId` is read from an environment variable the launcher sets and is
not checked against Steam. A whitelist that must hold against a modified client
should check the `ticket` with whatever issued it.

The Epic launcher sets `MafiaHubEpicId` from its signed-in account, and the client
includes that ID in the existing connection handshake. The server applies a hex
format check and exposes it as `PendingConnection.epicId` before admission and
`Player.epicId` after joining. It is an Epic account ID, not an EOS Product User
ID. No Epic proof or server-side account verification is performed.

A whitelist using the client-reported Epic ID can check it directly:

```js
Events.on("playerConnecting", async (connection) => {
    if (!connection.epicId || !await whitelist.containsEpicAccount(connection.epicId)) {
        connection.reject("Your Epic account is not on the whitelist.");
    }
});
```

---

## 5. Passing a ticket

The project puts it in `CurrentState::ticket` before `ConnectToServer`. KCDC
takes it from its launch link:

```
kcdc://play.example.org:27015?nickname=Jan&ticket=6f1c2e...
```

Percent-encode it (`+` decodes to a space). It is capped at
`ClientIdentity::kMaxTicketLength` (2048 bytes); a longer one is dropped whole
rather than cut.

---

## 6. Integrating a project

- **Server**: nothing to do. `InstanceOptions::admissionTimeoutMs`,
  `pendingConnections` and `pendingConnectionsPerAddress` tune it.
  `NetworkServer::SetSessionConfig` is still how a project publishes its session
  config; it is now sent with each accept.
- **Client**: set `CurrentState::ticket` if the project has a launch link.
  Override `OnAdmissionStatus` to show the server's lines while waiting; KCDC
  draws a card with a Leave button over its menu backdrop. A refusal arrives as
  `DisconnectionReason::CONNECTION_REFUSED` with the server's text.

This needs MafiaNet with the pending-session extensions, and is a netcode
change (MAJOR): the identity moved from a post-download RPC into the connection
request, and `ClientJoin` is new.
