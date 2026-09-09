# RPC identifiers

Every RPC the framework sends is named. RPC4 keys its slots by that name and
puts it on the wire, so before this feature a packet capture read like a table
of contents:

```
Framework::ChatMessage
Framework::ClientIdentity
M2O::PlayerShoot
M2O::VehicleExplodeRequest
```

The same literals sat in the shipped binary, one `strings` away. With transport
encryption off in the current build, that hands anyone watching the connection —
or holding the DLL — the complete RPC surface, including which calls exist that
a client is never supposed to send.

`FW_RPC_IDENTIFIER` replaces the readable name with an opaque, per-build token.
The source keeps the name; only the token reaches the binary and the wire.

```cpp
static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("M2O::PlayerReload");
// ships and transmits as "baabatlnofbdftgr"
```

Nothing else about writing an RPC changes.

> **Framework vs host mod.** Every framework RPC is already converted. Mod
> identifiers (`M2O::*`, `HogwartsMP::*`, …) are **opt-in** — they still travel
> in plaintext until a mod adopts the macro, which is what the rest of this
> document is about.

---

## 1. How it works

```
"M2O::PlayerReload"  ──►  SipHash-2-4 keyed by FW_RPC_IDENTIFIER_SALT  ──►  64-bit hash
                                                                              │
                                     4 bits per character, 16 characters      ▼
                          RPC4 slot key + wire  ◄──  "baabatlnofbdftgr"  ◄──  token
```

The name is only ever an argument to a template parameter, which C++ must
evaluate at compile time. It therefore has no runtime existence: the compiler
emits the finished token and nothing else. The token is a `static constexpr`
array in the class template, so `.data()` hands back a plain `const char *` with
static storage duration — exactly what RPC4's string-keyed API already wanted.

Two design choices are worth knowing about, because both look arbitrary until
you hit the thing they avoid.

**The hash is keyed, not salted.** The obvious cheap construction is FNV-1a with
the salt mixed into the initial state. It is also invertible: multiply by the
modular inverse of the FNV prime and unwind the name backwards, and you recover
the initial state, hence the salt. Every framework identifier name is public in
this repository, so one observed token next to its known name would give up the
salt and with it every other identifier in the build — which is precisely what
the salt exists to prevent. SipHash-2-4 offers no such shortcut. It is pinned
against its published reference vectors in `code/tests/modules/rpc_identifier_ut.h`.

**The token is not hex.** RPC4 writes the identifier with `WriteCompressed`,
which Huffman-codes it against RakNet's fixed *English* frequency table. Digits
are expensive there — `0`–`9` cost 10 to 16 bits each — so a 16-character hex
token would cost about 151 bits, *more* than most of the readable names it
replaces. The alphabet is instead the sixteen cheapest characters in that table:

```cpp
inline constexpr char kIdentifierAlphabet[] = "eaintoshlrudbcfg";
```

That caps a token at 96 bits, below the cheapest name it replaces (110 bits for
`Framework::SetOwner`), so hashing an identifier always makes the packet
smaller. A unit test enforces this through the real `WriteCompressed` path, so
editing the alphabet cannot silently regress it.

---

## 2. Using it in a mod

### 2.1 Declare the payload

Mod RPC headers usually include only `<mafianet/BitStream.h>`. Add the
identifier header — or `<networking/rpc/rpc.h>`, which pulls it in — and wrap
the name:

```cpp
#pragma once

#include <networking/rpc/rpc_identifier.h>

#include <mafianet/BitStream.h>

#include <cstdint>

namespace M2O::Shared::RPC {

    // Event-driven weapon reload: client -> server, relayed to the other clients.
    struct PlayerReload {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("M2O::PlayerReload");

        uint64_t networkId = 0; // reloading avatar entity

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, networkId);
        }
    };

} // namespace M2O::Shared::RPC
```

That is the entire change. `kIdentifier` is still a `static constexpr const char *`,
so the payload still satisfies the `Framework::Networking::RPC::Payload` concept
and every existing call site keeps compiling.

The header must live in the mod's **shared** tree. Client and server derive the
token independently from the same source line, and they only agree because they
compile the same string.

### 2.2 Register the handler

Unchanged — `RegisterRPC<T>` reads `T::kIdentifier` for you:

```cpp
// client/src/core/modules/human.cpp
net->RegisterRPC<Shared::RPC::PlayerReload>([](const Shared::RPC::PlayerReload &reload, MafiaNet::Packet *) {
    auto *human = Replication()->GetEntityByNetworkID(reload.networkId);
    // ... replay the reload on the remote ped
});
```

### 2.3 Send

Unchanged, in all three flavours:

```cpp
Shared::RPC::PlayerReload reload;
reload.networkId = GetNetworkID();

net->BroadcastRPC(reload);                                    // to everyone
net->SendRPC(reload, MafiaNet::ToGuid(human->ownerGUID));     // to one peer
```

Raw sends that pass the identifier themselves keep working too, because the
macro still produces a `const char *`:

```cpp
// server/src/core/rpc/player_handlers.cpp — relay to everyone but the sender
MafiaNet::BitStream bs;
relayed.Serialize(&bs, true);
netServer->SignalExcept(Shared::RPC::PlayerReload::kIdentifier, bs, packet->guid);
```

### 2.4 Naming rules

- **Keep names unique and prefixed** (`M2O::PlayerReload`, not `PlayerReload`).
  Two identical names now collide as one token instead of one string — the same
  bug, equally silent, but no longer visible in a capture.
- **Renaming an identifier is a wire break.** The token is derived from the exact
  bytes of the name, so `M2O::PlayerReload` → `M2O::PlayerReloaded` re-keys the
  slot and old peers stop dispatching it. Treat a rename like any other netcode
  change.
- **The argument must be a literal**, or at least a constant expression. A
  runtime `std::string` will not compile, which is deliberate: it is the same
  rule that keeps the readable name out of the binary.

### 2.5 Convert a mod incrementally

Identifiers are independent of each other — each one only has to match itself on
the other peer. A mod can convert one header at a time, as long as **client and
server ship together**, which a netcode change already requires. There is no
mixed-mode compatibility: a converted identifier and an unconverted one are
simply two different slot names.

---

## 3. The build salt

```cpp
#ifndef FW_RPC_IDENTIFIER_SALT
#define FW_RPC_IDENTIFIER_SALT 0x9E3779B97F4A7C15ULL
#endif
```

The salt keys the hash, so changing it re-derives *every* identifier at once. A
name table someone recovered by reverse-engineering one release is worthless
against the next.

The default is a fixed constant, so local dev builds are deterministic and
interoperate with each other without anyone configuring anything. Release CI
overrides it per build:

```bash
cmake -B build -DFW_RPC_IDENTIFIER_SALT=0x8F2A17C4D9B3E051
```

`code/framework/CMakeLists.txt` turns that cache variable into a `PUBLIC`
compile definition on the `Framework` target, so it reaches the preprocessor and
propagates to every downstream target automatically. A `ULL` suffix is accepted
but not required.

> Setting it as a bare CMake cache variable is not enough on its own — a cache
> variable never reaches the compiler by itself. This is why the wiring exists;
> do not try to route around it with `add_definitions` in a mod's own
> `CMakeLists.txt`, which would apply to the mod's targets but not to the
> framework's.

### Salt mismatch is caught at connect time

Both peers must derive identical tokens, and a mismatch has no error path of its
own: `Signal` to a slot the other side never registered is a no-op, so two peers
with different salts would complete the handshake and then silently exchange
nothing at all.

`NetworkPeer::BuildToken` therefore folds `RPC::IdentifierSaltTag()` into the
`TwoWayAuthentication` challenge, alongside the game and framework versions. A
salt mismatch is refused at the gate, the same way a version mismatch already is.
Mods get this for free — they already call `BuildToken`:

```cpp
net->SetBuildToken(Framework::Networking::NetworkPeer::BuildToken(
    _opts.gameName, _opts.gameVersion, Utils::Version::rel, _opts.modVersion));
```

---

## 4. Debugging

The cost of opacity is that logs and captures show `baabatlnofbdftgr` instead of
`M2O::PlayerReload`. The mapping is one-way at runtime, so recover it from the
source side rather than trying to invert a token.

Dump your own table in a dev build, right where the handlers are registered:

```cpp
#ifdef _DEBUG
    Framework::Logging::GetLogger("RPC")->debug("{} -> {}", "M2O::PlayerReload", Shared::RPC::PlayerReload::kIdentifier);
#endif
```

To test a hunch about a token seen in a capture, hash the candidate name in
place — it costs nothing at runtime and resolves at compile time:

```cpp
FW_RPC_IDENTIFIER("M2O::PlayerShoot")   // compare against the token you captured
```

Both approaches need the salt the build in question used, which is the point:
the mapping is only reconstructible by whoever can rebuild it.

---

## 5. Version semantics

Hashing an identifier changes the wire format, so it is a **MAJOR** bump under
the rules in `AGENTS.md` — client and server must be rebuilt together. For
framework changes that happens automatically: `.github/bump_version.sh` lists
`code/framework/src/networking/rpc` and `.../networking/replication` under
`major_paths`. A mod converting its own identifiers is responsible for its own
version bump.

---

## 6. What this does and does not protect

**It does:**

- remove readable RPC names from the shipped binary and from every packet,
- make the mapping rotate per release, so a table recovered once does not keep
  working,
- cost less on the wire than the names it replaced.

**It does not:**

- encrypt anything. Payload *contents* are still plaintext on the wire; the fix
  for that is enabling transport encryption (`LIBCAT_SECURITY` is currently `0`),
  which is tracked separately. Identifier hashing narrows the surface, it does
  not replace encryption.
- make an identifier secret at runtime. Anyone who can run the client can
  observe which token carries which behaviour by correlating traffic with what
  happens in game. The goal is removing the free, offline table — not preventing
  a determined attacker from rebuilding one.
- authenticate anything. An identifier is a routing key, never a permission
  check. A handler that trusts its sender was wrong before this change and is
  still wrong after it — validate `packet->guid` against the sender's entity, the
  way the framework's own handlers do.
- cover application data. `EmitScriptEvent`'s script-defined event *name* is a
  payload field, not an identifier, and stays readable. Entity type ids are a
  separate unsalted CRC32.
