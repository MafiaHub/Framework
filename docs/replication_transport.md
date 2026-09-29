# Replication transport

Framework 29 batches unreliable transforms and trims the work a busy peer does
per received packet. Client and server must run the same Framework protocol
major. MafiaNet 0.20.0 carries the parts that are not Framework-specific:
ReplicaManager3 no longer sends a message for a channel group with nothing
selected in it, TwoWayAuthentication has a configurable handshake timeout, and
StatisticsHistoryPlugin can sample at a fixed interval.

## Transform batches

`ReplicationConnection::SendSerialize` first withholds the transform channel
by the viewer's distance band (server). With batching enabled, the pose that
remains is queued into the connection's `TransformBatch`; the other channels,
and any pose the batch cannot carry, go through the standard RM3 serialize
message. The manager flushes every connection's batch at the end of the same
replication pass, so batching adds no delay. Every pose in a pass carries that
pass's timestamp; a timestamp, world or delivery-parameter change starts a new
batch.

Call `ReplicationManager::SetTransformBatchingEnabled(true)` before connecting.
Every receiver reads batches; senders opt in per game.

The batch is unreliable and byte-aligned:

```
ID_TIMESTAMP | Time | ID_USER_PACKET_ENUM | WorldId | uint16 entryCount
entryCount * (unsigned LEB128 NetworkID | uint16 poseBits | pose | zero pad)
```

`ID_USER_PACKET_ENUM` followed by a timestamp is reserved for the Framework:
a game's raw message identifiers start after it. A packet that is not a
timestamped batch is passed on untouched. Network IDs use canonical LEB128
(1-10 bytes), so the small IDs the server hands out cost one or two bytes.
Pose payloads are unchanged, including each entity's state epoch.

A batch is at most 1,100 bytes and at most the negotiated MTU minus 128 bytes
of transport headroom, so it is never fragmented. A pose too large for that,
reliable, or untimestamped is refused by the batch and sent as a normal RM3
message. The MTU lookup scans every peer slot, so each connection reads it once
per pass.

The receiver validates the whole packet before applying any entry. Unknown
connections and worlds, and malformed batches, are dropped. An entry for an
entity that does not exist yet (construction in flight) or any more
(destruction overtook the batch) is skipped without affecting the others. Each
entry goes through `Replica3::Deserialize`, so the owner, epoch and
per-entity timestamp checks of a normal serialize message apply unchanged.

## One replication pass per drain

MafiaNet updates every plugin on each `Receive()`, so draining N packets used
to run N ReplicaManager3 world passes. `NetworkPeer::Update` brackets its drain
with `BeginNetworkUpdate` / `EndNetworkUpdate`, and the manager runs its pass on
the first `Update()` inside the bracket only. That pass runs before the drain's
packets are handled; what they change is serialized by the next pass. Outside
the bracket every `Update()` runs a pass, as in ReplicaManager3.

`NetworkPeer` samples statistics history at most every 100 ms
(`kStatisticsSampleIntervalMs`), and gives the build-verification handshake 30
seconds (`kBuildVerificationTimeoutMs`) so a peer stalled by a load or a join
burst is not dropped as unverified. Both ends need the longer timeout: the
nonce the server hands out expires on the server.

## Staggered interest queries

With a nonzero interest rebuild interval, each viewer recomputes its interest
set once per interval at a stable, GUID-derived phase, even when the grid has
not changed, so the work spreads over the interval instead of landing on the
tick after a rebuild. Some changes skip the phase:

- every viewer, at once: an entity destroyed (a cached set must never hand out
  a deleted entity) or a rebuild caused by creation or destruction;
- one viewer, at once: its first query, a new viewer entity, a virtual-world
  change, or gaining or losing ownership of an entity (owned entities bypass
  range and budget, so only the two owners' sets change).

The default zero interval keeps generation-based refreshes. KCDC uses 100 ms:
ordinary discovery can take up to one grid interval plus one viewer interval.
Staggering spreads work across ticks shorter than the interval; it cannot split
a single long tick, and the grid itself is still rebuilt in one pass.

## Tests

`transform_batch_ut.h` covers the batch format end to end without sockets:
exact pose bits, budget splits, per-connection isolation, refusal of poses the
format cannot carry, malformed and truncated packets, pass-through of
non-batch packets, ownership, epoch and timestamp gates, and one pass per
drain. `network_work_ut.h` covers refresh phases and which changes skip them.
MafiaNet's own unit tests cover the empty-group, handshake-timeout and
sampling-interval behaviour.
