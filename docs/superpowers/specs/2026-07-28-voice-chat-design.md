# Voice Chat — Design

Date: 2026-07-28
Status: Approved, pending implementation plan

## Goal

Add customizable, scriptable voice chat to the MafiaHub Framework, built on MafiaNet's
`RakVoice` plugin (Opus; RNNoise is vendored but inactive in M1 — see Constraints), with a
working sample integration in the M2O project.

Reference implementations studied: FiveM (server-owned voice channels, submixes) and
MTA:SA (numeric channels, two-tier muting).

## Constraints from the existing code

`RakVoice` (`MafiaNet/Source/src/RakVoice.cpp`) provides Opus encode/decode, DTX voice
activity detection, VBR, 20ms framing, a per-peer jitter buffer, and packet-loss
concealment. It does **not** provide microphone capture, speaker output, 3D positioning,
channels, or permissions.

It also carries RNNoise wiring, but **that wiring does not run at this framework's frame
size and M1 therefore ships without denoising.** RakVoice denoises only when
`sampleRate == 48000 && frameSizeSamples == RNNOISE_FRAME_SIZE` (480, i.e. 10ms), while
`GetFrameSizeSamples()` returns `sampleRate / 50` — 960 samples, the VoIP-standard 20ms,
matching `Framework::Voice::kFrameSamples`. The denoiser is allocated per channel and
never invoked. The frame size is not being changed to suit it: 20ms is the right
latency/overhead trade-off and is what the whole pipeline is sized around. Resolving this
— dropping to 10ms frames, or running RNNoise twice per 20ms frame with shared state — is
an M2 decision.

Two structural facts drive the design:

1. `SendFrame(guid)` transmits peer-to-peer. In a dedicated-server game a client is
   connected only to the server, so player-to-player delivery is not directly reachable.
2. Channels are keyed by `packet->guid` (`RakVoice.cpp:692`) and `ReceiveFrame` sums every
   speaker into a single buffer (`RakVoice.cpp:465`). Under a server relay, every speaker
   would arrive under the server's GUID and collapse into one decoder; even corrected, the
   pre-mixed output leaves no way to position speakers individually.

## Decisions

| Axis | Decision | Rationale |
|---|---|---|
| Topology | Server relay, opaque frames | Server stays authoritative over who hears whom without running a codec. A hacked client cannot hear players it is not allowed to hear, because it never receives their bytes. |
| Playback | Framework-owned miniaudio mixer behind a sink interface | Portable across projects and testable headless; the interface leaves room for a game-engine sink later without rewriting transport, codec, or scripting. |
| Codec layer | Extend `RakVoice` in MafiaNet | Reuses a working jitter buffer, PLC and DTX instead of reimplementing them. (Its RNNoise wiring is *not* a reason: see Constraints — it is unreachable at 20ms frames and M1 ships undenoised.) |
| Scope | Proximity MVP first | The proximity path exercises the entire pipeline; channels and radio are policy layered on the same routing. |

Rejected: server-side decode/mix (roughly a full core burned on codecs at 64 players, plus
~20ms added latency); P2P mesh via NAT punchthrough (exposes player IPs, fails behind
symmetric NAT, and needs a relay fallback anyway).

Consequence of the relay choice: the server cannot apply audio effects. Effects such as the
radio filter are applied client-side, driven by a flag the server sets on the channel.

## 0. Prerequisite: re-vendoring MafiaNet

The Framework does not consume the standalone MafiaNet checkout. It vendors a copy in-tree
at `vendors/mafianet`, tracked in the Framework repository and pinned to tag v0.10.0
(commit `0ab32ce`, see `vendors/mafianet/VERSION.txt`). **That pin predates `RakVoice.cpp`,
which is therefore absent from the vendored copy** — `vendors/mafianet/Source/src/` has 109
files against the standalone repo's 111, missing `RakVoice.cpp` and `MmsgBatch.cpp`.

So voice work must begin by re-vendoring MafiaNet at a revision that includes RakVoice.
Additionally, MafiaNet fetches Opus and RNNoise via `cmake/FetchVoiceDependencies.cmake`,
and that `cmake/` directory is not part of the vendored subset. The Framework uses no
`FetchContent` anywhere; everything is vendored in-tree. Opus and RNNoise therefore get
vendored as `vendors/opus` and `vendors/rnnoise`, consistent with the surrounding
convention.

## 1. MafiaNet: `RakVoice` extensions

All four changes are additive and gated so existing peer-to-peer behaviour is unchanged.

### 1.1 Relay packet format

```
[ID_RAKVOICE_RELAY_DATA][RakNetGUID origin][uint16 channelId][uint16 seq][opus payload]
```

Overhead is 18 bytes per frame; at 50 frames/s that is ~7 kbps on top of Opus at 24–32 kbps.

### 1.2 Relay mode

`void SetRelayMode(bool enable)`.

- Client: `SendFrame` targets the server and stamps the local GUID as `origin`.
- Server: `void RelayFrame(Packet *packet, const RakNetGUID *recipients, int count)`
  forwards the payload with `origin` preserved. No encoder or decoder is initialised
  server-side — the operation is a header rewrite plus one `Send` per recipient.

### 1.3 Origin-keyed channels

In relay mode, `OnVoiceData` looks up the voice channel by the `origin` field rather than
`packet->guid`. Decoders are created lazily on the first frame from a speaker and freed
after a silence timeout or on disconnect.

### 1.4 Per-speaker output

- `void SetPerSpeakerOutput(bool enable)` — disables the internal mix at `RakVoice.cpp:465`.
- `bool ReceiveFrameFrom(RakNetGUID origin, void *out)` — pulls one decoded frame from that
  speaker's ring buffer.
- `void GetActiveSpeakers(DataStructures::List<RakNetGUID> &out)`.

A pull API is used rather than a decode callback: the ring buffer indices are not atomic, so
a push hook would pull threading concerns into MafiaNet. Pulling on the Framework's main
thread keeps that boundary clean.

## 2. Framework: module layout

```
code/framework/src/voice/
  voice_config.h            48kHz, 20ms frames (960 samples), bitrate, packet ids
  client/
    voice_client.{h,cpp}    owns RakVoice, PTT state, speaker registry
    audio_device.{h,cpp}    miniaudio capture + playback streams
    mixer.{h,cpp}           per-speaker gain/pan, distance curve, radio biquad
    i_voice_sink.h          Submit(speakerId, pcm, samples, position)
  server/
    voice_router.{h,cpp}    frame -> recipient set
    voice_channel.{h,cpp}   channel state, membership, mute/deaf
```

Registered through `CoreModules` as `SetVoice()` / `GetVoice()`, following the existing
pattern in `core_modules.h`.

miniaudio is vendored as a single header (no external dependencies; WASAPI on Windows,
CoreAudio on macOS, ALSA on Linux).

### 2.1 Threading model

miniaudio's capture and playback callbacks run on dedicated audio threads and must not
allocate or touch MafiaNet.

- Capture thread → lock-free SPSC ring → main tick pops frames, gates on push-to-talk and
  mute state, calls `SendFrame`.
- Main tick drains `ReceiveFrameFrom` per speaker → per-speaker lock-free SPSC queue →
  playback thread consumes, applies a gain/pan snapshot published atomically by the main
  thread, sums, and writes output.

Speaker positions and volumes cross the thread boundary as an immutable snapshot published
by the main thread, never as shared mutable state.

## 3. Server: routing

```cpp
struct VoiceChannel {
    uint16_t id;
    Mode mode;          // Proximity | Global | Group
    float maxDistance;  // Proximity only
    Effect effect;      // None | Radio — a hint the client applies
    std::unordered_set<GUID> members;
    std::unordered_set<GUID> globallyMuted;
};
```

Channel modes:

- `Proximity` — recipients are players within `maxDistance` of the talker.
- `Global` — recipients are all channel members, regardless of position or distance.
- `Group` — identical routing to `Global`; the distinction is that `Global` channels are
  intended as server-wide broadcast and `Group` channels as scripted subsets. They differ
  only in intent and in the defaults `createChannel` applies.

Players may belong to several channels simultaneously. Channel 0 is an implicit proximity
channel every player joins on connect. The recipient set for a talker is the union across
that talker's channels, minus each listener's local mute list and deaf flag.

`Voice.setPlayerVoiceRange(player, meters)` overrides `maxDistance` for that talker in
proximity channels only, and has no effect in `Global` or `Group` channels. When unset, the
channel's `maxDistance` applies.

Two properties keep routing cheap:

- Recipient sets are cached per talker and recomputed every 250ms rather than per frame. At
  50 frames/s per talker, per-frame recomputation would issue 50 spatial queries per second
  per talker for a set that changes slowly.
- Proximity queries run against a position map the router owns (`GUID -> vec3`), refreshed
  from the player list each tick, using a linear distance scan.

  This replaces an earlier intent to reuse `networking/replication/interest_grid`.
  `InterestGrid::QueryRadius` is private and is keyed on `NetworkEntity*` rather than player
  GUIDs, so reuse would mean widening its interface and translating entities back to
  players. At the target player counts a linear scan is cheaper than that coupling: 128
  players x 8 talkers x 4 Hz is ~4k distance checks per second. It also keeps the router a
  pure function of positions and membership, which is what makes it unit-testable without a
  server.

Bandwidth guard, as shipped in M1: **none beyond proximity.** The router returns every
eligible listener inside the talker's range, and the range is the only fan-out bound.
`kMaxAudibleTalkers` (default 6) survives as the client mixer's speaker-slot count — the
number of concurrent speakers one listener decodes and mixes — which incidentally bounds
a listener's decode cost but not its inbound bandwidth.

An earlier revision of this document described a per-listener cap on concurrent audible
talkers, and the first implementation truncated to the 6 nearest *recipients*, which is a
cap on how many listeners one talker reaches — the opposite thing, and silently mutes a
talker to everyone else in a crowd. That truncation has been removed. A genuine
per-listener bound cannot be computed per talker in isolation; it needs a listener-keyed
pass over all active talkers, and is an M2 item.

## 4. Script API

Server-side v8 builtin at `scripting/builtins/voice.{h,cpp}`, following the structure of
`scripting/builtins/chat.h`.

```js
Voice.createChannel({ mode: 'proximity'|'global'|'group', maxDistance?, effect? })  // -> id
Voice.deleteChannel(id)
Voice.addPlayer(id, player)
Voice.removePlayer(id, player)
Voice.getPlayers(id)
Voice.getPlayerChannels(player)
Voice.setPlayerMuted(player, bool)          // server-wide, authoritative
Voice.setPlayerDeaf(player, bool)
Voice.setPlayerVoiceRange(player, meters)   // whisper / normal / shout
Voice.isPlayerTalking(player)
```

Client-side:

```js
Voice.setLocalMuted(player, bool)           // local playback only
Voice.getInputDevices()
Voice.setInputDevice(name)
Voice.setInputVolume(v)
Voice.setOutputVolume(v)
Voice.setActivationMode('ptt'|'voice')
Voice.setPushToTalkKey(key)
```

Events: `playerVoiceStart` / `playerVoiceStop` server-side, `voiceStart` / `voiceStop`
client-side.

This combines FiveM's server-owned channel model with MTA's two-tier muting (server-wide
versus per-listener).

## 5. M2O integration

- `code/projects/m2o/code/client/src/core/modules/voice.cpp` — publishes the listener
  transform from the camera each frame, binds the default push-to-talk key, and draws a
  speaking indicator above talking players.
- `code/projects/m2o/resources/m2o-demo/radio.js` — `/radio <freq>` joins a group channel
  with the radio effect; vehicle occupants auto-join a shared channel. Exercises both
  channel modes and the effect flag.

No M2O server-side code is required; the scripting builtin lives in the Framework.

## 6. Milestones

**M1 — proximity MVP.** Capture → encode → relay → route by distance →
per-speaker decode → 3D mix → output. Push-to-talk and local mute. This is the complete
pipeline; everything after it is policy on top of routing.

**M2 — channels.** Server-owned channels, radio effect, full script API, demo resource.

**M3 — polish.** Device selection UI, speaking indicators, voice-activation mode.

## 7. Testing

- Unit tests in `code/tests/` for the router's recipient-set computation: pure logic over
  positions and channel membership, requiring no audio device.
- Codec roundtrip verified through `RakVoice::SetLoopbackMode`.
- Manual verification with two clients on localhost.

## 8. Version impact

New packet IDs plus a `RakVoice` header change require client and server to update
together. Per the project's version semantics this is a **MAJOR** bump.
