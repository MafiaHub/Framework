# Replication transport

Framework 29 adds optional transform batching and removes empty RM3 channel
messages. Client and server must use the same Framework protocol major.

`ReplicationConnection` filters the selected transform by the existing distance
bands, then passes the selection to `ReplicationWriter`. The writer sends only
selected, nonempty channels, grouped by their delivery parameters. Cached bytes
on an unselected channel never cause a message. Reliable state retains the
standard RM3 field-delta format and is sent immediately.

Call `ReplicationManager::SetTransformBatchingEnabled(true)` before connecting
to opt into batched transforms. Receivers support both formats. Each connection
owns its batch; the manager flushes it before returning from the same replication
update. All poses use that update's timestamp. A timestamp, world or delivery
parameter change flushes the previous batch.

The batch is unreliable and has this byte-aligned layout:

```
ID_TIMESTAMP | Time | ID_USER_PACKET_ENUM | WorldId | uint16 entryCount
entryCount * (unsigned LEB128 NetworkID | uint16 poseBits | pose | zero pad)
```

`ID_USER_PACKET_ENUM` is reserved for Framework transform batches. Game-specific
raw message identifiers must begin after it. The timestamp prefix uses MafiaNet's
normal clock adjustment. Pose payloads are unchanged, including each entity's
state epoch. Network IDs use canonical unsigned LEB128 (1-10 bytes); small IDs
cost one or two bytes regardless of BitStream's integer endianness.

Packets are at most 1,100 bytes and at most the negotiated MTU minus 128 bytes of
transport headroom. A full batch is flushed before adding another entry; a single
pose too large for the budget uses the existing RM3 fragmentation path. No batching
delay, distance-band change, quantization change or new pose delta baseline is
introduced.

The receiver validates the complete bounded envelope before dispatching entries.
Unknown connections/worlds and malformed batches are discarded. Missing entities
are skipped independently, allowing construction/destruction races without losing
the other poses. The existing `NetworkEntity::Deserialize` applies owner, epoch
and per-entity timestamp checks. Connection teardown discards its queued batch.

`replication_writer_ut.h` exercises the actual packet writer and receiver without
opening sockets: stale state buffers, state-only sends, bit-exact framing, MTU
splits, receiver isolation, malformed data, ownership and ordering. KCDC's
`transform_batch_ut.h` round-trips 100 real player pose serializers and compares
their application bytes against the corrected unbatched RM3 path. These are
deterministic protocol checks, not measurements of live UDP traffic or gameplay.

## Processing cost and joins

`NetworkPeer::Update` scopes its packet drain with `BeginNetworkUpdate` and
`EndNetworkUpdate`. MafiaNet calls every plugin's `Update` at each `Receive`;
the Framework replication manager performs its world/construction/serialize
pass only on the first such call in that drain. Incoming packet handlers
still run for every packet. Newly authenticated connections and entities
created during the drain enter the next application's replication pass.
Standalone manager `Update` calls retain their normal behavior.

Each connection reads the negotiated MTU once per serialization pass and
invalidates that cached budget when flushing. This avoids MafiaNet's linear
peer-slot scan for every entity while still observing MTU changes on the
next pass.

Framework's `BuildAuthentication` keeps MafiaNet's challenge/response wire
protocol and verification, but expires outgoing challenges and server-side
nonces after 30 seconds. Both endpoints should be rebuilt: increasing only
the client's timeout leaves the old server's short nonce lifetime in place.
Disconnect and shutdown cleanup remain inherited from MafiaNet. KCDCStress
uses a separate 60-second overall join deadline and configurable, bounded
concurrent joins; it never treats a timed-out verification as success.

## Bounded receive work and staggered interest queries

`SampledStatisticsHistory` samples MafiaNet's history at most once per 100 ms,
including during a busy receive drain. New/lost connection tracking still uses
the original plugin callbacks. Live peer counters (including stress-test RX/TX)
are unaffected. A late update takes one sample, without a catch-up loop.

The transform batch decoder uses fixed stack storage for at most 275 entries,
bounded by the existing 1,100-byte packet limit. It validates the whole envelope
before publishing any entries. One `DeserializeParameters` and its pose stream
are reused within the packet, retaining any grown capacity and resetting the
read cursor and bit length between entities. Pose bytes are still copied into
that stream; they are not retained as pointers into receive packet storage.

With a nonzero interest rebuild interval, viewer queries have stable GUID-based
phases across the same interval. They refresh even when the grid generation
has not changed, so deadlines do not bunch up behind a grid rebuild. Initial
viewers, changed viewer identity/dimension, entity removals, ownership changes,
and rebuilds caused by entity creation/destruction bypass the phase delay.
The default zero interval retains generation-based refreshes.

KCDC uses 100 ms: ordinary spatial discovery can take up to one grid interval
plus one viewer interval, with additional delay if the server itself stalls.
Existing in-range replicas continue receiving poses at their configured rate.
This spreads routine work across ticks when ticks are shorter than the interval;
it cannot spread work within a single 180 ms tick or eliminate the immediate
work of mass joins. The grid itself is still rebuilt in one pass. The 64 visible
body binding limit is unchanged.
