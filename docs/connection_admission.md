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
    const entry = await whitelist.lookup(connection.accountId); // DB, HTTP, anything async
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
  |                                               |  account signature checked; a bad one is refused here
  |                                               |  playerConnecting ... handlers settle
  |<-- ID_SESSION_CONFIG_STATUS (update(), 0..n) -|  restarts the session timeout on both ends
  |<-- ID_SESSION_CONFIG (session config) --------|  AcceptSession: the connection now exists
  |    or ID_SESSION_CONFIG_REJECTED (reason) ----|  RejectSession: it never will
  | -- build challenge, resource list, download, ClientJoin: unchanged
```

The identity (`RPC::ClientIdentity`: nickname, ids, ticket, account proof) is
the client's MafiaNet **session payload**, carried in the connection request
itself. The
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
| The account proof is missing, malformed or does not verify | refused: "Your client could not prove its account. Update it and try again." -- before any script runs |
| The request was signed more than 5 minutes from the server's clock | refused, telling the player how far their clock is off |
| The same signed request arrives twice | refused: "This connection request was already used. Try again." |
| One IP address asks more than `connectionAttemptBurst` (16) times at once, then more than `connectionAttemptRate` (0.2) a second | refused: "Too many connection attempts from your address. Wait a minute and try again." -- before the signature is checked |
| The server already remembers 65536 requests still inside their 5 minutes | refused: "The server is busy checking connections. Try again in a minute." -- a remembered request is never forgotten early, or it could be replayed |
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
| `accountId` | the account the player **proved** (section 5); the same value `player.accountId` reads once they are in |
| `unverified` | frozen `{ steamId, epicId, discordId, discordName, hardwareId }`, as reported by the client; empty strings when absent |
| `ticket` | the `ticket` of the client's launch link, untouched |
| `ip` | remote address, no port |
| `reject(reason?)` | refuse; reason up to 512 bytes |
| `update(message)` | status line up to 256 bytes; restarts the timeout |
| `isPending()` | false once decided or the player left |

**Only `accountId` is verified.** Everything under `unverified` is what the
client said about itself: `steamId` is read from an environment variable the
launcher sets and is not checked against Steam, and a Discord id is public, so a
modified client can claim anyone's. Reading the old flat names
(`connection.discordId`, `player.steamId`, ...) throws, pointing at
`accountId`, so a script written against them fails where it trusted one.

---

## 5. Accounts

A player's account is a key pair their client keeps, not an id it reports.

- The client creates an Ed25519 key the first time it connects
  (`InstanceOptions::accountKeyPath`, by default `<cacheRoot>/account.key`) and
  signs every connection request with it: all the fields above, the time, and a
  random nonce (`networking/account_key.h`).
- The server checks the signature on the request as it arrived, before it
  sanitizes anything or raises `playerConnecting`. It refuses a request signed
  more than `AccountProofVerifier::kMaxClockSkewSeconds` (5 minutes) from its own
  clock, and one it has already let through.
- `accountId` is the first 128 bits of SHA-256 over the public key, as 32 hex
  characters: the same on every server, and no client can produce it without the
  private key.

What it does not cover:

- **Platform accounts.** It proves the player holds a key, not that they own a
  Steam or Discord account. Link those to an `accountId` some other way (a
  Discord OAuth flow on your website, an in-game code) before trusting the link.
- **A lost key is a lost account.** Nothing recovers it. A project should tell
  players where it is (`Instance::GetAccountKeyPath`) so they can back it up.
- **The wire is not encrypted.** Someone who can intercept and block a player's
  traffic can use the request before the player does, within its 5 minutes. So
  can a malicious server the player is connecting to, by relaying the request
  to another server live. Closing that needs an encrypted transport the
  signature is bound to.

---

## 6. Passing a ticket

The project puts it in `CurrentState::ticket` before `ConnectToServer`. KCDC
takes it from its launch link:

```
kcdc://play.example.org:27015?nickname=Jan&ticket=6f1c2e...
```

Percent-encode it (`+` decodes to a space). It is capped at
`ClientIdentity::kMaxTicketLength` (2048 bytes); a longer one is dropped whole
rather than cut.

---

## 7. Integrating a project

- **Server**: nothing to do. `InstanceOptions::admissionTimeoutMs`,
  `pendingConnections` and `pendingConnectionsPerAddress` tune it, and
  `connectionAttemptRate` / `connectionAttemptBurst` bound requests per address
  (raise them when many players share one, as at a LAN event).
  `NetworkServer::SetSessionConfig` is still how a project publishes its session
  config; it is now sent with each accept.
- **Client**: point `InstanceOptions::accountKeyPath` somewhere players do not
  clear to fix a download, and show them `GetAccountId()` so they can give it to
  staff. Set `CurrentState::ticket` if the project has a launch link.
  Override `OnAdmissionStatus` to show the server's lines while waiting; KCDC
  draws a card with a Leave button over its menu backdrop. A refusal arrives as
  `DisconnectionReason::CONNECTION_REFUSED` with the server's text.

This needs MafiaNet with the pending-session extensions, and is a netcode
change (MAJOR): the identity moved from a post-download RPC into the connection
request, and `ClientJoin` is new.
