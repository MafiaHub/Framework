# Voice Chat — M1 Proximity MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Players hear nearby players' voices, positioned in 3D, over a server-relayed Opus stream, with push-to-talk and per-listener mute.

**Architecture:** Clients capture microphone audio, denoise and encode it with Opus via MafiaNet's `RakVoice` plugin, and send 20ms frames to the server. The server never runs a codec — it looks up which players are in range of the talker and forwards the opaque payload with the talker's GUID stamped in the header. Receiving clients decode one stream per speaker, apply distance attenuation and stereo panning against the local listener transform, and mix into a single output stream.

**Tech Stack:** C++20, MafiaNet (`RakVoice`, Opus, RNNoise), miniaudio (vendored single header), glm, v8pp, CMake.

**Scope:** This plan covers milestone M1 from the spec only. M2 (server-owned channels, radio effect, full script API, demo resource) and M3 (device selection UI, speaking indicators, voice activation) get their own plans once M1 is proven.

**Spec:** `docs/superpowers/specs/2026-07-28-voice-chat-design.md`

## Global Constraints

- C++ standard is **C++20** (`CMakeLists.txt:6`), `CMAKE_CXX_STANDARD_REQUIRED ON`.
- Audio format is fixed for M1: **48000 Hz, mono, 20ms frames = 960 samples = 1920 bytes** of `int16_t` PCM.
- Opus target bitrate **24000 bps**, VBR on, DTX on, RNNoise on.
- Every new Framework source file must be added explicitly to the correct list in `code/framework/CMakeLists.txt` (`FRAMEWORK_SRC`, `FRAMEWORK_CLIENT_SRC`, or `FRAMEWORK_SERVER_SRC`). The Framework does **not** glob sources.
- Third-party code is **vendored in-tree** under `vendors/`. Do not introduce `FetchContent` — the Framework uses none.
- Namespaces use the form `namespace Framework::Voice {` closed with `} // namespace Framework::Voice`.
- Header guards are `#pragma once`. Private members take a `_` prefix, camelCase. Classes are PascalCase and `final` where possible.
- Run `scripts/format_codebase.sh` before every commit. `vendors/` is excluded from formatting.
- Commit messages follow `Module: Brief commit description`, wrapped at 72 characters.
- **Building:** on Windows use `builds\build.bat <target> 64` only — never invoke `cmake --build` directly and never create ad-hoc build directories. On macOS/Linux use `cmake -B build && cmake --build build`.
- Audio callbacks must never allocate, never lock, and never call into MafiaNet.

---

## File Structure

**Vendored (new):**
- `vendors/opus/` — Opus codec, static.
- `vendors/rnnoise/` — RNNoise noise suppression, static.
- `vendors/miniaudio/miniaudio.h` — single-header audio device backend.
- `vendors/mafianet/Source/{src,include/mafianet}/RakVoice.{cpp,h}` — re-vendored from MafiaNet.

**Framework — shared (`FRAMEWORK_SRC`):**
- `code/framework/src/voice/voice_config.h` — format constants and packet identifiers. Header-only.

**Framework — server (`FRAMEWORK_SERVER_SRC`):**
- `code/framework/src/voice/server/voice_router.{h,cpp}` — pure recipient-set computation. No networking, no audio. This is the unit-tested core.
- `code/framework/src/voice/server/voice_server.{h,cpp}` — wires the router to `NetworkServer` and `RakVoice`.

**Framework — client (`FRAMEWORK_CLIENT_SRC`):**
- `code/framework/src/voice/client/i_voice_sink.h` — the swap point for a game-engine audio sink.
- `code/framework/src/voice/client/spsc_ring.h` — lock-free single-producer/single-consumer PCM ring. Header-only, unit-tested.
- `code/framework/src/voice/client/mixer.{h,cpp}` — distance attenuation and stereo panning. Pure math, unit-tested.
- `code/framework/src/voice/client/audio_device.{h,cpp}` — miniaudio capture and playback streams.
- `code/framework/src/voice/client/voice_client.{h,cpp}` — owns `RakVoice`, push-to-talk state, per-speaker queues, and the default sink.

**Tests:**
- `code/tests/modules/voice_router_ut.h`
- `code/tests/modules/voice_mixer_ut.h`
- `code/tests/modules/spsc_ring_ut.h`

**M2O:**
- `code/projects/m2o/code/client/src/core/modules/voice.{h,cpp}` — listener transform and push-to-talk binding.

**Modified:**
- `vendors/mafianet/Source/include/mafianet/RakVoice.h` and `Source/src/RakVoice.cpp`
- `vendors/CMakeLists.txt`
- `code/framework/CMakeLists.txt`
- `code/framework/src/core_modules.h`
- `code/framework/src/integrations/server/instance.cpp`
- `code/framework/src/integrations/client/instance.cpp`
- `code/tests/framework_ut.cpp`
- `code/tests/CMakeLists.txt`
- `code/projects/m2o/code/client/CMakeLists.txt`

---

## Task 1: Vendor the voice dependencies

Bring `RakVoice` and its codec dependencies into the Framework's vendored tree. Nothing in later tasks compiles without this.

**Files:**
- Create: `vendors/opus/` (copied source tree), `vendors/rnnoise/` (copied source tree), `vendors/miniaudio/miniaudio.h`
- Create: `vendors/mafianet/Source/src/RakVoice.cpp`, `vendors/mafianet/Source/include/mafianet/RakVoice.h`
- Modify: `vendors/CMakeLists.txt`, `vendors/mafianet/CMakeLists.txt`, `vendors/mafianet/VERSION.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: CMake targets `opus`, `rnnoise`, `miniaudio` (INTERFACE), and a `MafiaNet` target that compiles `RakVoice.cpp`. The header `<mafianet/RakVoice.h>` becomes includable.

- [ ] **Step 1: Copy RakVoice into the vendored MafiaNet**

The standalone checkout at `/Users/enguerrand/dev/mafiahub/MafiaNet` contains `RakVoice`; the vendored copy (pinned to v0.10.0) does not.

```bash
cp /Users/enguerrand/dev/mafiahub/MafiaNet/Source/src/RakVoice.cpp \
   vendors/mafianet/Source/src/RakVoice.cpp
cp /Users/enguerrand/dev/mafiahub/MafiaNet/Source/include/mafianet/RakVoice.h \
   vendors/mafianet/Source/include/mafianet/RakVoice.h
```

`vendors/mafianet/CMakeLists.txt` globs `Source/src/*.cpp`, so no source list edit is needed.

- [ ] **Step 2: Record the re-vendoring**

Append to `vendors/mafianet/VERSION.txt`:

```
Additionally vendored (not in v0.10.0): Source/src/RakVoice.cpp and
Source/include/mafianet/RakVoice.h, taken from MafiaNet master for voice chat
support. Opus and RNNoise are vendored separately under vendors/opus and
vendors/rnnoise rather than fetched, because the Framework uses no FetchContent.
```

- [ ] **Step 3: Vendor Opus**

```bash
git clone --depth 1 --branch v1.5.2 https://github.com/xiph/opus.git /tmp/opus
rm -rf /tmp/opus/.git
mkdir -p vendors/opus && cp -R /tmp/opus/. vendors/opus/
```

- [ ] **Step 4: Vendor RNNoise**

```bash
git clone --depth 1 https://github.com/xiph/rnnoise.git /tmp/rnnoise
rm -rf /tmp/rnnoise/.git
mkdir -p vendors/rnnoise && cp -R /tmp/rnnoise/. vendors/rnnoise/
```

- [ ] **Step 5: Vendor miniaudio**

```bash
mkdir -p vendors/miniaudio
curl -L -o vendors/miniaudio/miniaudio.h \
  https://raw.githubusercontent.com/mackron/miniaudio/0.11.21/miniaudio.h
```

- [ ] **Step 6: Add a CMakeLists for miniaudio**

Create `vendors/miniaudio/CMakeLists.txt`:

```cmake
# miniaudio - single-header audio capture/playback backend.
# The implementation is compiled once inside FrameworkClient
# (audio_device.cpp defines MINIAUDIO_IMPLEMENTATION).
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio INTERFACE ${CMAKE_DL_LIBS} Threads::Threads m)
endif()
```

- [ ] **Step 7: Wire the three vendors into the build**

In `vendors/CMakeLists.txt`, insert directly **above** the existing `add_subdirectory(mafianet)` line (MafiaNet links Opus and RNNoise, so they must be configured first):

```cmake
# Build Opus and RNNoise (voice codec + noise suppression, used by RakVoice)
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
add_subdirectory(opus)
add_subdirectory(rnnoise)

# Build miniaudio (client audio device I/O)
add_subdirectory(miniaudio)
```

`rnnoise` upstream ships no CMakeLists; create `vendors/rnnoise/CMakeLists.txt`:

```cmake
# RNNoise - neural-network noise suppression used by RakVoice.
file(GLOB RNNOISE_SRC "src/*.c")
list(FILTER RNNOISE_SRC EXCLUDE REGEX "(dump_features|write_weights)\\.c$")

add_library(rnnoise STATIC ${RNNOISE_SRC})
target_include_directories(rnnoise PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(rnnoise PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(rnnoise PRIVATE /w)
else()
    target_compile_options(rnnoise PRIVATE -w)
endif()
```

- [ ] **Step 8: Link Opus and RNNoise into MafiaNet**

In `vendors/mafianet/CMakeLists.txt`, extend the existing `target_link_libraries` call:

```cmake
target_link_libraries(MafiaNet PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads opus rnnoise)
```

- [ ] **Step 9: Build and verify**

Windows: `builds\build.bat Framework 64`
macOS/Linux: `cmake -B build && cmake --build build --target Framework`

Expected: configure succeeds, `opus`, `rnnoise` and `MafiaNet` all build, and `RakVoice.cpp` appears in the compile output.

If configuration fails because Opus headers are not found by RNNoise, add to `vendors/rnnoise/CMakeLists.txt`:

```cmake
target_include_directories(rnnoise PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../opus/celt"
    "${CMAKE_CURRENT_SOURCE_DIR}/../opus/include")
target_compile_definitions(rnnoise PRIVATE COMPILE_OPUS=1)
```

- [ ] **Step 10: Commit**

```bash
git add vendors/opus vendors/rnnoise vendors/miniaudio vendors/mafianet vendors/CMakeLists.txt
git commit -m "Vendors: add Opus, RNNoise, miniaudio and RakVoice"
```

---

## Task 2: RakVoice relay protocol

Teach `RakVoice` to work through a relay: carry the talker's GUID in the packet, key decoders by that GUID, forward frames without decoding, and expose per-speaker PCM. All changes are gated so existing peer-to-peer behaviour is byte-identical when relay mode is off.

**Files:**
- Modify: `vendors/mafianet/Source/include/mafianet/RakVoice.h`, `vendors/mafianet/Source/src/RakVoice.cpp`

**Interfaces:**
- Consumes: `MafiaNet::RakVoice` as vendored in Task 1.
- Produces:
  - `void RakVoice::SetRelayMode(bool enable)`
  - `void RakVoice::SetRelayTarget(RakNetGUID server)`
  - `void RakVoice::RelayFrame(Packet *packet, const RakNetGUID *recipients, int count)`
  - `void RakVoice::SetPerSpeakerOutput(bool enable)`
  - `bool RakVoice::ReceiveFrameFrom(RakNetGUID origin, void *out)`
  - `void RakVoice::GetActiveSpeakers(DataStructures::List<RakNetGUID> &out)`
  - `static RakNetGUID RakVoice::ReadRelayOrigin(Packet *packet)`
  - Packet id `ID_RAKVOICE_RELAY_DATA`

- [ ] **Step 1: Add the relay packet identifier**

In `vendors/mafianet/Source/include/mafianet/MessageIdentifiers.h`, find the existing `ID_RAKVOICE_DATA` entry and add immediately after it:

```cpp
	/// RakVoice relay frame: origin GUID + channel + sequence + Opus payload.
	ID_RAKVOICE_RELAY_DATA,
```

Adding an identifier shifts every subsequent id. That is acceptable only because client and server ship together — this is the MAJOR version bump the spec calls for.

- [ ] **Step 2: Declare the new API**

In `RakVoice.h`, add to the public section (after `SetLoopbackMode`):

```cpp
	/// \brief Routes all outgoing frames through a relay server instead of directly to peers.
	/// In relay mode SendFrame() sends to the relay target and stamps our own GUID as the
	/// frame origin, and incoming frames are keyed by that origin rather than the sender.
	void SetRelayMode(bool enable);

	/// \brief Sets the server that relay-mode frames are sent to.
	void SetRelayTarget(RakNetGUID server);

	/// \brief Forwards a received relay frame to a set of recipients without decoding it.
	/// Costs one Send per recipient; no codec state is touched.
	void RelayFrame(Packet *packet, const RakNetGUID *recipients, int count);

	/// \brief Reads the origin GUID out of an ID_RAKVOICE_RELAY_DATA packet.
	static RakNetGUID ReadRelayOrigin(Packet *packet);

	/// \brief Stops ReceiveFrame() mixing speakers together, so each can be pulled separately.
	void SetPerSpeakerOutput(bool enable);

	/// \brief Pulls one decoded frame for a single speaker.
	/// \param[out] out Buffer of at least GetBufferSizeBytes() bytes.
	/// \return true if a frame was written, false if that speaker has no buffered audio.
	bool ReceiveFrameFrom(RakNetGUID origin, void *out);

	/// \brief Lists speakers that currently have a decoder allocated.
	void GetActiveSpeakers(DataStructures::List<RakNetGUID> &out);
```

And to the protected section:

```cpp
	VoiceChannel *GetOrCreateChannel(RakNetGUID origin);
	void OnRelayVoiceData(Packet *packet);

	bool relayMode;
	bool perSpeakerOutput;
	RakNetGUID relayTarget;
```

- [ ] **Step 3: Initialise the new state**

In the `RakVoice::RakVoice()` constructor in `RakVoice.cpp`, alongside the existing initialisers:

```cpp
	relayMode = false;
	perSpeakerOutput = false;
	relayTarget = UNASSIGNED_RAKNET_GUID;
```

- [ ] **Step 4: Implement the simple setters and the origin reader**

```cpp
void RakVoice::SetRelayMode(bool enable)
{
	relayMode = enable;

	// SendFrame() requires an open channel to hold the encoder and the outgoing ring, and
	// the normal way to get one is the RequestVoiceChannel handshake with a peer. There is
	// no peer to handshake with in relay mode, so open a channel keyed on our own GUID and
	// use it purely as local encoder state. Only meaningful once Init() has run, so the
	// client calls SetRelayMode before Init and this is re-checked there.
	if (enable && rakPeerInterface != nullptr && IsInitialized())
	{
		GetOrCreateChannel(rakPeerInterface->GetMyGUID());
	}
}

void RakVoice::SetRelayTarget(RakNetGUID server)
{
	relayTarget = server;
}

void RakVoice::SetPerSpeakerOutput(bool enable)
{
	perSpeakerOutput = enable;
}

RakNetGUID RakVoice::ReadRelayOrigin(Packet *packet)
{
	if (packet == nullptr || packet->length < 1 + sizeof(uint64_t))
		return UNASSIGNED_RAKNET_GUID;

	RakNetGUID origin;
	memcpy(&origin.g, packet->data + 1, sizeof(uint64_t));
	return origin;
}
```

- [ ] **Step 5: Implement the relay forward**

```cpp
void RakVoice::RelayFrame(Packet *packet, const RakNetGUID *recipients, int count)
{
	// Forward the payload untouched. The origin GUID is already in the header, written by
	// the talker, so the server does not need to rewrite anything and never decodes.
	if (packet == nullptr || rakPeerInterface == nullptr)
		return;

	for (int i = 0; i < count; i++)
	{
		rakPeerInterface->Send((const char*)packet->data, packet->length,
			MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable, 0, recipients[i], false);
	}
}
```

`Unreliable` — **not** `UnreliableSequenced` — is correct on this hop, and this is what shipped. Sequencing would be right if each speaker had its own stream, but every relayed frame reaches a recipient from a single sender (the server) on ordering channel 0, so all speakers share one sequence counter and whichever speaker loses the race gets discarded whenever two people talk at once. The relay header carries a per-speaker sequence number that already drives PLC, so ordering is resolved a layer up. A late frame is still worse than a lost one; that is handled by the per-speaker jitter/PLC logic, not by the transport.

(The client→server hop in Step 6 below is a different case: there is only one speaker on it, so `UnreliableSequenced` remains correct there.)

- [ ] **Step 6: Send relay frames from Update()**

In `RakVoice::Update()`, the encode path currently builds `tempOutput` with `tempOutput[0] = ID_RAKVOICE_DATA` and a 3-byte header (`headerSize`). Add a relay variant. Where the frame is assembled and sent, branch:

```cpp
	if (relayMode)
	{
		// [id][origin guid][channelId][seq][payload]
		static const int relayHeaderSize =
			sizeof(unsigned char) + sizeof(uint64_t) + sizeof(uint16_t) + sizeof(unsigned short);
		RakNetGUID self = rakPeerInterface->GetMyGUID();
		uint16_t channelId = 0; // M1: proximity only. M2 sets this per channel.

		tempOutput[0] = ID_RAKVOICE_RELAY_DATA;
		memcpy(tempOutput + 1, &self.g, sizeof(uint64_t));
		memcpy(tempOutput + 1 + sizeof(uint64_t), &channelId, sizeof(uint16_t));
		memcpy(tempOutput + 1 + sizeof(uint64_t) + sizeof(uint16_t),
			&channel->outgoingMessageNumber, sizeof(unsigned short));
		memcpy(tempOutput + relayHeaderSize, encodedBuffer, bytesWritten);

		rakPeerInterface->Send(tempOutput, bytesWritten + relayHeaderSize,
			HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 0, relayTarget, false);
	}
	else
	{
		// ... existing ID_RAKVOICE_DATA send, unchanged ...
	}
```

- [ ] **Step 7: Route incoming relay packets**

In `RakVoice::OnReceive`, add a case beside the existing `ID_RAKVOICE_DATA` case:

```cpp
	case ID_RAKVOICE_RELAY_DATA:
		OnRelayVoiceData(packet);
		return RR_STOP_PROCESSING_AND_DEALLOCATE;
```

- [ ] **Step 8: Implement origin-keyed decode**

```cpp
VoiceChannel *RakVoice::GetOrCreateChannel(RakNetGUID origin)
{
	bool objectExists;
	unsigned index = voiceChannels.GetIndexFromKey(origin, &objectExists);
	if (objectExists)
		return voiceChannels[index];

	// Lazily allocate a decoder the first time we hear from this speaker. Channels are
	// reaped by OnClosedConnection and by the silence timeout in Update().
	Packet synthetic;
	synthetic.guid = origin;
	OpenChannel(&synthetic);

	index = voiceChannels.GetIndexFromKey(origin, &objectExists);
	return objectExists ? voiceChannels[index] : nullptr;
}

void RakVoice::OnRelayVoiceData(Packet *packet)
{
	static const int relayHeaderSize =
		sizeof(unsigned char) + sizeof(uint64_t) + sizeof(uint16_t) + sizeof(unsigned short);

	if (packet->length <= (unsigned)relayHeaderSize)
		return;

	RakNetGUID origin = ReadRelayOrigin(packet);
	if (origin == UNASSIGNED_RAKNET_GUID || origin == rakPeerInterface->GetMyGUID())
		return;

	VoiceChannel *channel = GetOrCreateChannel(origin);
	if (channel == nullptr)
		return;

	// Reuse the existing decode path by rewriting the packet into the legacy layout the
	// non-relay decoder expects: [id][seq][payload], keyed on the channel we just resolved.
	unsigned short packetMessageNumber;
	memcpy(&packetMessageNumber, packet->data + 1 + sizeof(uint64_t) + sizeof(uint16_t),
		sizeof(unsigned short));

	DecodeIntoChannel(channel, packetMessageNumber,
		packet->data + relayHeaderSize, packet->length - relayHeaderSize);
}
```

- [ ] **Step 9: Extract the shared decode body**

The decode logic (sequence gap detection, PLC loop, `opus_decode`, `WriteOutputToChannel`) currently lives inline in `OnVoiceData`. Extract it so both paths share it. Add to the protected section of `RakVoice.h`:

```cpp
	void DecodeIntoChannel(VoiceChannel *channel, unsigned short packetMessageNumber,
		const unsigned char *payload, unsigned payloadLength);
```

Move the body of `OnVoiceData` after its channel lookup into `DecodeIntoChannel`, and reduce `OnVoiceData` to:

```cpp
void RakVoice::OnVoiceData(Packet *packet)
{
	bool objectExists;
	static const int headerSize = sizeof(unsigned char) + sizeof(unsigned short);

	unsigned index = voiceChannels.GetIndexFromKey(packet->guid, &objectExists);
	if (!objectExists)
		return;

	unsigned short packetMessageNumber;
	memcpy(&packetMessageNumber, packet->data + 1, sizeof(unsigned short));

	DecodeIntoChannel(voiceChannels[index], packetMessageNumber,
		packet->data + headerSize, packet->length - headerSize);
}
```

This is a pure refactor: the non-relay path must behave exactly as before.

- [ ] **Step 9b: Open the local encoder channel at the end of Init()**

`SetRelayMode` may be called before `Init()`, in which case there is no buffer to allocate a
channel against yet. Add to the end of `RakVoice::Init()`:

```cpp
	if (relayMode && rakPeerInterface != nullptr)
	{
		// Local encoder state for the outgoing stream; see SetRelayMode.
		GetOrCreateChannel(rakPeerInterface->GetMyGUID());
	}
```

Also skip the self-keyed channel when reporting speakers, since it holds outgoing audio
rather than a remote talker. In `GetActiveSpeakers`:

```cpp
	RakNetGUID self = rakPeerInterface != nullptr ? rakPeerInterface->GetMyGUID() : UNASSIGNED_RAKNET_GUID;
	for (unsigned i = 0; i < voiceChannels.Size(); i++)
	{
		if (voiceChannels[i]->guid == self)
			continue;
		out.Push(voiceChannels[i]->guid, _FILE_AND_LINE_);
	}
```

- [ ] **Step 10: Suppress the internal mix when per-speaker output is on**

In `RakVoice::Update()`, the block around the mixing loop (`bufferedOutput[j] += in[j % ...]`) runs unconditionally. Guard the mix so the ring buffers fill but nothing is summed:

```cpp
	if (perSpeakerOutput == false)
	{
		// ... existing per-channel drain and mix into bufferedOutput ...
	}
```

- [ ] **Step 11: Implement per-speaker pull**

```cpp
bool RakVoice::ReceiveFrameFrom(RakNetGUID origin, void *out)
{
	bool objectExists;
	unsigned index = voiceChannels.GetIndexFromKey(origin, &objectExists);
	if (!objectExists)
		return false;

	VoiceChannel *channel = voiceChannels[index];
	unsigned totalBufferSize = bufferSizeBytes * FRAME_INCOMING_BUFFER_COUNT;

	unsigned available = (channel->incomingWriteIndex >= channel->incomingReadIndex)
		? channel->incomingWriteIndex - channel->incomingReadIndex
		: totalBufferSize - channel->incomingReadIndex + channel->incomingWriteIndex;

	if (available < bufferSizeBytes)
		return false;

	char *dst = (char*)out;
	unsigned firstChunk = totalBufferSize - channel->incomingReadIndex;
	if (firstChunk >= bufferSizeBytes)
	{
		memcpy(dst, channel->incomingBuffer + channel->incomingReadIndex, bufferSizeBytes);
	}
	else
	{
		memcpy(dst, channel->incomingBuffer + channel->incomingReadIndex, firstChunk);
		memcpy(dst + firstChunk, channel->incomingBuffer, bufferSizeBytes - firstChunk);
	}

	channel->incomingReadIndex += bufferSizeBytes;
	if (channel->incomingReadIndex >= totalBufferSize)
		channel->incomingReadIndex -= totalBufferSize;

	return true;
}

void RakVoice::GetActiveSpeakers(DataStructures::List<RakNetGUID> &out)
{
	out.Clear(false, _FILE_AND_LINE_);
	for (unsigned i = 0; i < voiceChannels.Size(); i++)
		out.Push(voiceChannels[i]->guid, _FILE_AND_LINE_);
}
```

- [ ] **Step 12: Build and verify**

Windows: `builds\build.bat Framework 64`
macOS/Linux: `cmake --build build --target Framework`

Expected: MafiaNet compiles clean. There is no automated test at this layer — RakVoice needs a live `RakPeerInterface`, so it is covered by the loopback check in Task 9 and by manual two-client verification at the end.

- [ ] **Step 13: Commit**

```bash
git add vendors/mafianet
git commit -m "MafiaNet: add RakVoice relay mode and per-speaker output"
```

---

## Task 3: Voice format constants

A single header both sides agree on, so no magic numbers get duplicated across the client and server.

**Files:**
- Create: `code/framework/src/voice/voice_config.h`
- Modify: `code/framework/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `Framework::Voice::kSampleRate`, `kFrameSamples`, `kFrameBytes`, `kChannels`, `kBitrate`, `kMaxAudibleTalkers`, `kRecipientRefreshMs`, `kDefaultProximityRange`, `kSpeakerSilenceTimeoutMs`.

- [ ] **Step 1: Write the header**

Create `code/framework/src/voice/voice_config.h`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>

namespace Framework::Voice {
    // Opus operates natively at 48kHz; 20ms frames are the standard trade-off between
    // latency and per-packet header overhead. Mono, because voice is positioned by the
    // client mixer rather than carried as stereo.
    constexpr uint32_t kSampleRate   = 48000;
    constexpr uint32_t kChannels     = 1;
    constexpr uint32_t kFrameSamples = 960;                          // 20ms at 48kHz
    constexpr uint32_t kFrameBytes   = kFrameSamples * sizeof(int16_t);

    // Target Opus bitrate. 24kbps is transparent for speech and keeps a full lobby of
    // talkers within a few hundred kbps of server egress.
    constexpr uint32_t kBitrate = 24000;

    // Per-listener cap on simultaneously audible talkers, nearest first. Without it a
    // crowded spawn point floods every client's downstream.
    constexpr uint32_t kMaxAudibleTalkers = 6;

    // Recipient sets are recomputed on this interval rather than per frame; at 50 frames
    // per second per talker, per-frame recomputation would be 50x the work for a set that
    // changes slowly.
    constexpr uint32_t kRecipientRefreshMs = 250;

    // Default proximity audibility radius, in world units.
    constexpr float kDefaultProximityRange = 25.0f;

    // A speaker's decoder is released after this long without a frame.
    constexpr uint32_t kSpeakerSilenceTimeoutMs = 2000;
} // namespace Framework::Voice
```

- [ ] **Step 2: Verify it compiles standalone**

```bash
c++ -std=c++20 -fsyntax-only -Icode/framework/src code/framework/src/voice/voice_config.h
```

Expected: no output.

- [ ] **Step 3: Commit**

```bash
scripts/format_codebase.sh
git add code/framework/src/voice/voice_config.h
git commit -m "Voice: add shared audio format constants"
```

---

## Task 4: Server voice router

The routing decision, as a pure function of positions and mute state. No networking, no audio, no MafiaNet — which is exactly what makes it testable.

**Files:**
- Create: `code/framework/src/voice/server/voice_router.h`, `code/framework/src/voice/server/voice_router.cpp`
- Test: `code/tests/modules/voice_router_ut.h`
- Modify: `code/framework/CMakeLists.txt`, `code/tests/framework_ut.cpp`, `code/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Framework::Voice::kMaxAudibleTalkers`, `kDefaultProximityRange` from Task 3.
- Produces:
  - `Framework::Voice::VoiceRouter`
  - `void SetPlayerPosition(uint64_t guid, const glm::vec3 &pos)`
  - `void RemovePlayer(uint64_t guid)`
  - `void SetPlayerRange(uint64_t guid, float meters)`
  - `void SetPlayerMuted(uint64_t guid, bool muted)`
  - `void SetLocalMute(uint64_t listener, uint64_t target, bool muted)`
  - `void SetPlayerDeaf(uint64_t guid, bool deaf)`
  - `void ComputeRecipients(uint64_t talker, std::vector<uint64_t> &out) const`

- [ ] **Step 1: Write the failing test**

Create `code/tests/modules/voice_router_ut.h`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/server/voice_router.h"

#include <algorithm>

namespace {
    bool RouterContains(const std::vector<uint64_t> &v, uint64_t id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    }
} // namespace

MODULE(voice_router, {
    using namespace Framework::Voice;

    IT("delivers to a player inside the proximity range", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(10, 0, 0));

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0], static_cast<uint64_t>(2));
    });

    IT("excludes a player outside the proximity range", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(500, 0, 0));

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(0));
    });

    IT("never delivers a talker's own voice back to them", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(0));
    });

    IT("honours a per-talker range override", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(60, 0, 0));
        router.SetPlayerRange(1, 100.0f);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(1));
    });

    IT("drops every recipient when the talker is server-muted", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));
        router.SetPlayerMuted(1, true);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(0));
    });

    IT("skips a listener who locally muted the talker", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));
        router.SetPlayerPosition(3, glm::vec3(5, 0, 0));
        router.SetLocalMute(2, 1, true);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(RouterContains(out, 3), true);
    });

    IT("skips a deaf listener", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));
        router.SetPlayerDeaf(2, true);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(0));
    });

    IT("forgets a removed player", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));
        router.RemovePlayer(2);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(0));
    });

    IT("returns nothing for an unknown talker", {
        VoiceRouter router;
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));

        std::vector<uint64_t> out;
        router.ComputeRecipients(99, out);

        EQUALS(out.size(), static_cast<size_t>(0));
    });
});
```

- [ ] **Step 2: Register the test module**

In `code/tests/framework_ut.cpp`, add the include beside the other test category includes:

```cpp
#include "modules/voice_router_ut.h"
```

and the registration beside the other `UNIT_MODULE` calls:

```cpp
    UNIT_MODULE(voice_router);
```

- [ ] **Step 3: Run the test to verify it fails**

Windows: `builds\build.bat FrameworkTests 64`
macOS/Linux: `cmake --build build --target RunFrameworkTests`

Expected: compilation FAILS with `voice/server/voice_router.h: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `code/framework/src/voice/server/voice_router.h`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/voice_config.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Framework::Voice {
    // Decides who hears a given talker. Deliberately free of networking and audio: it is a
    // pure function of positions, ranges and mute state, so the routing rule can be tested
    // without standing up a server. VoiceServer owns one of these and feeds it positions.
    //
    // Proximity is evaluated with a linear scan rather than through the replication interest
    // grid: InterestGrid::QueryRadius is private and keyed on NetworkEntity rather than
    // player GUIDs, and at realistic player counts the scan is cheaper than that coupling.
    class VoiceRouter final {
      public:
        // Upserts a player's world position. Also registers a previously unknown player.
        void SetPlayerPosition(uint64_t guid, const glm::vec3 &pos);

        // Drops all state for a player: position, range, mute flags, and every local-mute
        // entry naming them, so a reused GUID cannot inherit a stale mute.
        void RemovePlayer(uint64_t guid);

        // Overrides the audibility radius for one talker (whisper / normal / shout).
        // Pass a value <= 0 to fall back to kDefaultProximityRange.
        void SetPlayerRange(uint64_t guid, float meters);

        // Server-wide mute: a muted talker reaches nobody.
        void SetPlayerMuted(uint64_t guid, bool muted);

        // Listener-side mute: `listener` stops receiving `target`.
        void SetLocalMute(uint64_t listener, uint64_t target, bool muted);

        // A deaf listener receives nobody.
        void SetPlayerDeaf(uint64_t guid, bool deaf);

        // Fills `out` with the GUIDs that should receive `talker`'s frames. Clears `out`
        // first. NOTE: the cap described here was removed before merge -- every eligible
        // listener in range is returned, in unspecified order. See the note in Task 8.
        void ComputeRecipients(uint64_t talker, std::vector<uint64_t> &out) const;

      private:
        struct PlayerState {
            glm::vec3 position {0.0f};
            float range      = 0.0f; // <= 0 means kDefaultProximityRange
            bool serverMuted = false;
            bool deaf        = false;
            std::unordered_set<uint64_t> locallyMuted; // talkers this player does not hear
        };

        std::unordered_map<uint64_t, PlayerState> _players;
    };
} // namespace Framework::Voice
```

- [ ] **Step 5: Write the implementation**

Create `code/framework/src/voice/server/voice_router.cpp`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "voice_router.h"

#include <algorithm>

namespace Framework::Voice {
    void VoiceRouter::SetPlayerPosition(uint64_t guid, const glm::vec3 &pos) {
        _players[guid].position = pos;
    }

    void VoiceRouter::RemovePlayer(uint64_t guid) {
        _players.erase(guid);

        // A local mute naming the departed player must go too, or a recycled GUID would
        // silently inherit it.
        for (auto &entry : _players) {
            entry.second.locallyMuted.erase(guid);
        }
    }

    void VoiceRouter::SetPlayerRange(uint64_t guid, float meters) {
        _players[guid].range = meters;
    }

    void VoiceRouter::SetPlayerMuted(uint64_t guid, bool muted) {
        _players[guid].serverMuted = muted;
    }

    void VoiceRouter::SetLocalMute(uint64_t listener, uint64_t target, bool muted) {
        auto &state = _players[listener];
        if (muted) {
            state.locallyMuted.insert(target);
        }
        else {
            state.locallyMuted.erase(target);
        }
    }

    void VoiceRouter::SetPlayerDeaf(uint64_t guid, bool deaf) {
        _players[guid].deaf = deaf;
    }

    void VoiceRouter::ComputeRecipients(uint64_t talker, std::vector<uint64_t> &out) const {
        out.clear();

        const auto talkerIt = _players.find(talker);
        if (talkerIt == _players.end() || talkerIt->second.serverMuted) {
            return;
        }

        const auto &talkerState = talkerIt->second;
        const float range       = talkerState.range > 0.0f ? talkerState.range : kDefaultProximityRange;
        const float rangeSq     = range * range;

        // Collected with distance so the cap can keep the nearest listeners.
        std::vector<std::pair<float, uint64_t>> candidates;
        candidates.reserve(_players.size());

        for (const auto &[guid, state] : _players) {
            if (guid == talker || state.deaf) {
                continue;
            }
            if (state.locallyMuted.count(talker) != 0) {
                continue;
            }

            const glm::vec3 delta = state.position - talkerState.position;
            const float distSq    = glm::dot(delta, delta);
            if (distSq > rangeSq) {
                continue;
            }

            candidates.emplace_back(distSq, guid);
        }

        // REMOVED BEFORE MERGE -- this truncated to the nearest kMaxAudibleTalkers
        // *recipients*, which caps how many listeners a talker reaches rather than how
        // many talkers a listener hears. Shipped code keeps every candidate. See Task 8.
        if (candidates.size() > kMaxAudibleTalkers) {
            std::partial_sort(candidates.begin(), candidates.begin() + kMaxAudibleTalkers, candidates.end());
            candidates.resize(kMaxAudibleTalkers);
        }

        out.reserve(candidates.size());
        for (const auto &[distSq, guid] : candidates) {
            out.push_back(guid);
        }
    }
} // namespace Framework::Voice
```

- [ ] **Step 6: Add the source to the build**

In `code/framework/CMakeLists.txt`, append to `FRAMEWORK_SERVER_SRC`:

```cmake
    src/voice/server/voice_router.cpp
```

- [ ] **Step 7: Run the tests to verify they pass**

Windows: `builds\build.bat FrameworkTests 64` then run the produced `FrameworkTests` executable.
macOS/Linux: `cmake --build build --target RunFrameworkTests`

Expected: all nine `voice_router` cases PASS.

- [ ] **Step 8: Commit**

```bash
scripts/format_codebase.sh
git add code/framework/src/voice/server code/framework/CMakeLists.txt \
        code/tests/modules/voice_router_ut.h code/tests/framework_ut.cpp
git commit -m "Voice: add server-side proximity voice router"
```

---

## Task 5: Lock-free PCM ring

The single piece of thread-crossing machinery in the design. Written and tested on its own so the audio-thread tasks can rely on it.

**Files:**
- Create: `code/framework/src/voice/client/spsc_ring.h`
- Test: `code/tests/modules/spsc_ring_ut.h`
- Modify: `code/tests/framework_ut.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `Framework::Voice::SpscRing<T, Capacity>` with `bool Push(const T *src, size_t count)`, `bool Pop(T *dst, size_t count)`, `size_t Available() const`, `void Clear()`.

- [ ] **Step 1: Write the failing test**

Create `code/tests/modules/spsc_ring_ut.h`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/client/spsc_ring.h"

MODULE(spsc_ring, {
    using namespace Framework::Voice;

    IT("returns what was pushed, in order", {
        SpscRing<int16_t, 64> ring;
        const int16_t in[4] = {1, 2, 3, 4};
        EQUALS(ring.Push(in, 4), true);

        int16_t out[4] = {0, 0, 0, 0};
        EQUALS(ring.Pop(out, 4), true);
        EQUALS(out[0], static_cast<int16_t>(1));
        EQUALS(out[3], static_cast<int16_t>(4));
    });

    IT("reports how much is readable", {
        SpscRing<int16_t, 64> ring;
        const int16_t in[3] = {7, 8, 9};
        ring.Push(in, 3);
        EQUALS(ring.Available(), static_cast<size_t>(3));
    });

    IT("refuses a pop larger than what is buffered", {
        SpscRing<int16_t, 64> ring;
        const int16_t in[2] = {1, 2};
        ring.Push(in, 2);

        int16_t out[4] = {0, 0, 0, 0};
        EQUALS(ring.Pop(out, 4), false);
        EQUALS(ring.Available(), static_cast<size_t>(2));
    });

    IT("refuses a push that would overflow", {
        SpscRing<int16_t, 8> ring;
        const int16_t in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        EQUALS(ring.Push(in, 8), false);
    });

    IT("wraps around the end of the buffer", {
        SpscRing<int16_t, 8> ring;
        const int16_t first[5] = {1, 2, 3, 4, 5};
        int16_t scratch[5]     = {0, 0, 0, 0, 0};

        ring.Push(first, 5);
        ring.Pop(scratch, 5);

        const int16_t second[5] = {6, 7, 8, 9, 10};
        EQUALS(ring.Push(second, 5), true);
        EQUALS(ring.Pop(scratch, 5), true);
        EQUALS(scratch[0], static_cast<int16_t>(6));
        EQUALS(scratch[4], static_cast<int16_t>(10));
    });

    IT("is empty after being cleared", {
        SpscRing<int16_t, 64> ring;
        const int16_t in[3] = {1, 2, 3};
        ring.Push(in, 3);
        ring.Clear();
        EQUALS(ring.Available(), static_cast<size_t>(0));
    });
});
```

- [ ] **Step 2: Register the test module**

In `code/tests/framework_ut.cpp` add `#include "modules/spsc_ring_ut.h"` and `UNIT_MODULE(spsc_ring);`.

- [ ] **Step 3: Run to verify failure**

Expected: compilation FAILS with `voice/client/spsc_ring.h: No such file or directory`.

- [ ] **Step 4: Implement the ring**

Create `code/framework/src/voice/client/spsc_ring.h`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>

namespace Framework::Voice {
    // Wait-free ring for exactly one producer thread and one consumer thread. Used to carry
    // PCM across the audio-callback boundary, where allocating or locking would risk an
    // underrun. One slot is always left empty so a full buffer is distinguishable from an
    // empty one without a separate count.
    //
    // Capacity must be a power of two so the wrap is a mask rather than a modulo.
    template <typename T, size_t Capacity>
    class SpscRing final {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

      public:
        // Producer side. Returns false and writes nothing if the data would not fit.
        bool Push(const T *src, size_t count) {
            const size_t write = _write.load(std::memory_order_relaxed);
            const size_t read  = _read.load(std::memory_order_acquire);
            const size_t free  = Capacity - 1 - ((write - read) & kMask);

            if (count > free) {
                return false;
            }

            for (size_t i = 0; i < count; i++) {
                _buffer[(write + i) & kMask] = src[i];
            }

            _write.store(write + count, std::memory_order_release);
            return true;
        }

        // Consumer side. Returns false and writes nothing if fewer than `count` are buffered.
        bool Pop(T *dst, size_t count) {
            const size_t read  = _read.load(std::memory_order_relaxed);
            const size_t write = _write.load(std::memory_order_acquire);

            if (((write - read) & kMask) < count) {
                return false;
            }

            for (size_t i = 0; i < count; i++) {
                dst[i] = _buffer[(read + i) & kMask];
            }

            _read.store(read + count, std::memory_order_release);
            return true;
        }

        // Consumer side. Elements currently readable.
        size_t Available() const {
            const size_t write = _write.load(std::memory_order_acquire);
            const size_t read  = _read.load(std::memory_order_relaxed);
            return (write - read) & kMask;
        }

        // Not safe against a concurrent producer or consumer; call only when both are stopped.
        void Clear() {
            _read.store(0, std::memory_order_relaxed);
            _write.store(0, std::memory_order_relaxed);
        }

      private:
        static constexpr size_t kMask = Capacity - 1;

        std::array<T, Capacity> _buffer {};
        std::atomic<size_t> _read {0};
        std::atomic<size_t> _write {0};
    };
} // namespace Framework::Voice
```

- [ ] **Step 5: Run the tests to verify they pass**

macOS/Linux: `cmake --build build --target RunFrameworkTests`

Expected: all six `spsc_ring` cases PASS.

- [ ] **Step 6: Commit**

```bash
scripts/format_codebase.sh
git add code/framework/src/voice/client/spsc_ring.h \
        code/tests/modules/spsc_ring_ut.h code/tests/framework_ut.cpp
git commit -m "Voice: add lock-free SPSC ring for audio thread handoff"
```

---

## Task 6: 3D mixer math

Distance attenuation and stereo panning, as pure functions. Tested without any audio device.

**Files:**
- Create: `code/framework/src/voice/client/mixer.h`, `code/framework/src/voice/client/mixer.cpp`
- Test: `code/tests/modules/voice_mixer_ut.h`
- Modify: `code/framework/CMakeLists.txt`, `code/tests/framework_ut.cpp`, `code/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `kDefaultProximityRange` from Task 3.
- Produces:
  - `struct Framework::Voice::ListenerTransform { glm::vec3 position; glm::vec3 forward; glm::vec3 up; }`
  - `struct Framework::Voice::SpeakerGain { float left; float right; }`
  - `SpeakerGain ComputeGain(const ListenerTransform &listener, const glm::vec3 &speakerPos, float range)`
  - `void MixFrameInto(float *stereoOut, const int16_t *monoIn, uint32_t samples, SpeakerGain gain)`

- [ ] **Step 1: Write the failing test**

Create `code/tests/modules/voice_mixer_ut.h`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/client/mixer.h"

#include <cmath>

namespace {
    Framework::Voice::ListenerTransform OriginListener() {
        Framework::Voice::ListenerTransform t;
        t.position = glm::vec3(0.0f, 0.0f, 0.0f);
        t.forward  = glm::vec3(0.0f, 0.0f, 1.0f);
        t.up       = glm::vec3(0.0f, 1.0f, 0.0f);
        return t;
    }

    bool NearlyEqual(float a, float b) {
        return std::fabs(a - b) < 0.02f;
    }
} // namespace

MODULE(voice_mixer, {
    using namespace Framework::Voice;

    IT("plays a co-located speaker at equal, unattenuated volume in both ears", {
        // Constant-power panning puts a centred speaker at cos(45 degrees) per ear, which is
        // ~0.707 and not 1.0 — that is what keeps loudness flat as a speaker pans across.
        const auto gain = ComputeGain(OriginListener(), glm::vec3(0.0f, 0.0f, 0.0f), 25.0f);
        EQUALS(NearlyEqual(gain.left, 0.707f), true);
        EQUALS(NearlyEqual(gain.right, 0.707f), true);
    });

    IT("silences a speaker beyond the range", {
        const auto gain = ComputeGain(OriginListener(), glm::vec3(0.0f, 0.0f, 100.0f), 25.0f);
        EQUALS(gain.left, 0.0f);
        EQUALS(gain.right, 0.0f);
    });

    IT("attenuates with distance", {
        const auto near = ComputeGain(OriginListener(), glm::vec3(0.0f, 0.0f, 5.0f), 25.0f);
        const auto far  = ComputeGain(OriginListener(), glm::vec3(0.0f, 0.0f, 20.0f), 25.0f);
        EQUALS(near.left > far.left, true);
    });

    IT("pans a speaker on the right louder in the right ear", {
        const auto gain = ComputeGain(OriginListener(), glm::vec3(5.0f, 0.0f, 0.0f), 25.0f);
        EQUALS(gain.right > gain.left, true);
    });

    IT("pans a speaker on the left louder in the left ear", {
        const auto gain = ComputeGain(OriginListener(), glm::vec3(-5.0f, 0.0f, 0.0f), 25.0f);
        EQUALS(gain.left > gain.right, true);
    });

    IT("keeps a speaker dead ahead centred", {
        const auto gain = ComputeGain(OriginListener(), glm::vec3(0.0f, 0.0f, 5.0f), 25.0f);
        EQUALS(NearlyEqual(gain.left, gain.right), true);
    });

    IT("accumulates a frame into the stereo output", {
        float out[4]         = {0.0f, 0.0f, 0.0f, 0.0f};
        const int16_t in[2]  = {16384, -16384};
        const SpeakerGain g  = {1.0f, 0.5f};

        MixFrameInto(out, in, 2, g);

        EQUALS(NearlyEqual(out[0], 0.5f), true);   // sample 0, left
        EQUALS(NearlyEqual(out[1], 0.25f), true);  // sample 0, right
        EQUALS(NearlyEqual(out[2], -0.5f), true);  // sample 1, left
        EQUALS(NearlyEqual(out[3], -0.25f), true); // sample 1, right
    });

    IT("sums two speakers rather than replacing", {
        float out[2]        = {0.0f, 0.0f};
        const int16_t in[1] = {16384};
        const SpeakerGain g = {1.0f, 1.0f};

        MixFrameInto(out, in, 1, g);
        MixFrameInto(out, in, 1, g);

        EQUALS(NearlyEqual(out[0], 1.0f), true);
    });
});
```

- [ ] **Step 2: Register the test module**

In `code/tests/framework_ut.cpp` add `#include "modules/voice_mixer_ut.h"` and `UNIT_MODULE(voice_mixer);`.

- [ ] **Step 3: Run to verify failure**

Expected: compilation FAILS with `voice/client/mixer.h: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `code/framework/src/voice/client/mixer.h`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Framework::Voice {
    // Where the local player is listening from. Published by the game each frame; consumed
    // by the audio thread through an atomically swapped snapshot.
    struct ListenerTransform {
        glm::vec3 position {0.0f};
        glm::vec3 forward {0.0f, 0.0f, 1.0f};
        glm::vec3 up {0.0f, 1.0f, 0.0f};
    };

    // Per-ear linear gain for one speaker, in [0, 1].
    struct SpeakerGain {
        float left  = 0.0f;
        float right = 0.0f;
    };

    // Distance attenuation and constant-power stereo pan for one speaker relative to the
    // listener. Returns silence beyond `range`. Pure: no state, safe on the audio thread.
    SpeakerGain ComputeGain(const ListenerTransform &listener, const glm::vec3 &speakerPos, float range);

    // Accumulates `samples` mono int16 samples into an interleaved stereo float buffer,
    // applying `gain`. Adds rather than assigns so several speakers can be layered.
    // `stereoOut` must hold at least samples * 2 floats.
    void MixFrameInto(float *stereoOut, const int16_t *monoIn, uint32_t samples, SpeakerGain gain);
} // namespace Framework::Voice
```

- [ ] **Step 5: Write the implementation**

Create `code/framework/src/voice/client/mixer.cpp`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "mixer.h"

#include <algorithm>
#include <cmath>

namespace Framework::Voice {
    namespace {
        constexpr float kInt16Scale = 1.0f / 32768.0f;

        // Below this distance a speaker is at full volume; attenuation starts beyond it.
        // Without it, a speaker standing on top of the listener produces a division blow-up
        // and an unpleasant volume spike as they cross the origin.
        constexpr float kMinDistance = 1.0f;

        // How far the pan is allowed to swing. A full hard pan sounds wrong on headphones
        // for a speaker only slightly off-axis, so the effect is deliberately partial.
        constexpr float kMaxPan = 0.6f;
    } // namespace

    SpeakerGain ComputeGain(const ListenerTransform &listener, const glm::vec3 &speakerPos, float range) {
        SpeakerGain gain;

        const glm::vec3 delta = speakerPos - listener.position;
        const float distance  = glm::length(delta);

        if (range <= 0.0f || distance > range) {
            return gain; // silent
        }

        // Inverse-distance rolloff, normalised so it reaches zero exactly at `range` rather
        // than trailing off asymptotically and leaving a faint always-audible tail.
        const float clamped    = std::max(distance, kMinDistance);
        const float rolloff    = kMinDistance / clamped;
        const float edgeFade   = 1.0f - (distance / range);
        const float attenuation = std::clamp(rolloff * edgeFade, 0.0f, 1.0f);

        // Pan on the listener's right axis. cross(up, forward) — not cross(forward, up),
        // which yields the left axis in a right-handed system and inverts the whole pan.
        // Degenerate transforms fall back to centred.
        float pan = 0.0f;
        if (distance > 0.0001f) {
            const glm::vec3 right = glm::cross(listener.up, listener.forward);
            const float rightLen  = glm::length(right);
            if (rightLen > 0.0001f) {
                pan = glm::dot(delta / distance, right / rightLen) * kMaxPan;
            }
        }

        // Constant-power pan: gains follow a quarter-circle so total energy stays flat as a
        // speaker sweeps across, instead of dipping in the middle as linear panning does.
        // A centred speaker therefore sits at cos(45 degrees) = ~0.707 per ear, not 1.0 —
        // that is the property that keeps perceived loudness constant, so it is not
        // normalised away.
        const float angle = (pan + 1.0f) * 0.25f * 3.14159265358979323846f;

        gain.left  = std::clamp(attenuation * std::cos(angle), 0.0f, 1.0f);
        gain.right = std::clamp(attenuation * std::sin(angle), 0.0f, 1.0f);
        return gain;
    }

    void MixFrameInto(float *stereoOut, const int16_t *monoIn, uint32_t samples, SpeakerGain gain) {
        for (uint32_t i = 0; i < samples; i++) {
            const float sample = static_cast<float>(monoIn[i]) * kInt16Scale;
            stereoOut[i * 2]     += sample * gain.left;
            stereoOut[i * 2 + 1] += sample * gain.right;
        }
    }
} // namespace Framework::Voice
```

- [ ] **Step 6: Add the source to the build**

In `code/framework/CMakeLists.txt`, append to `FRAMEWORK_CLIENT_SRC`:

```cmake
    src/voice/client/mixer.cpp
```

- [ ] **Step 7: Link FrameworkClient into the test binary**

`code/tests/CMakeLists.txt` currently links only `Framework` and `FrameworkServer`, so `mixer.cpp` would not be available. Rather than pulling the whole client library (and CEF with it) into the tests, compile the one file directly:

```cmake
add_executable(FrameworkTests framework_ut.cpp ../framework/src/voice/client/mixer.cpp)
```

- [ ] **Step 8: Run the tests to verify they pass**

macOS/Linux: `cmake --build build --target RunFrameworkTests`

Expected: all eight `voice_mixer` cases PASS, plus the previously added `voice_router` and `spsc_ring` modules.

- [ ] **Step 9: Commit**

```bash
scripts/format_codebase.sh
git add code/framework/src/voice/client/mixer.h code/framework/src/voice/client/mixer.cpp \
        code/framework/CMakeLists.txt code/tests/modules/voice_mixer_ut.h \
        code/tests/framework_ut.cpp code/tests/CMakeLists.txt
git commit -m "Voice: add 3D attenuation and stereo pan mixer"
```

---

## Task 7: Audio device

Microphone capture and speaker playback via miniaudio. Not unit-testable — it needs real hardware — so it is verified by a standalone loopback check that the task includes.

**Files:**
- Create: `code/framework/src/voice/client/audio_device.h`, `code/framework/src/voice/client/audio_device.cpp`
- Modify: `code/framework/CMakeLists.txt`

**Interfaces:**
- Consumes: `SpscRing` (Task 5), `kSampleRate`/`kFrameSamples` (Task 3).
- Produces:
  - `Framework::Voice::AudioDevice`
  - `bool Init()`, `void Shutdown()`
  - `bool PopCapturedFrame(int16_t *out)` — main thread; returns one 960-sample mono frame
  - `void SetPlaybackCallback(std::function<void(float *stereoOut, uint32_t frameCount)> cb)`
  - `bool IsRunning() const`

- [ ] **Step 1: Write the header**

Create `code/framework/src/voice/client/audio_device.h`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "spsc_ring.h"
#include "voice/voice_config.h"

#include <cstdint>
#include <functional>

struct ma_device;

namespace Framework::Voice {
    // Owns the microphone and speaker streams. miniaudio runs each callback on its own
    // thread, so neither may allocate, lock, or touch MafiaNet: capture pushes into a
    // lock-free ring that the main thread drains, and playback calls a callback that must
    // itself only read pre-published state.
    class AudioDevice final {
      public:
        AudioDevice()  = default;
        ~AudioDevice();

        AudioDevice(const AudioDevice &)            = delete;
        AudioDevice &operator=(const AudioDevice &) = delete;

        // Opens the default capture and playback devices at kSampleRate. Returns false if
        // either fails; voice is then simply unavailable and the rest of the client runs on.
        bool Init();
        void Shutdown();

        bool IsRunning() const {
            return _running;
        }

        // Main thread. Writes exactly kFrameSamples mono samples into `out`.
        // Returns false when a full frame has not been captured yet.
        bool PopCapturedFrame(int16_t *out);

        // Installs the mixing callback invoked on the playback thread. `stereoOut` holds
        // frameCount * 2 interleaved floats, pre-zeroed. Set before Init().
        void SetPlaybackCallback(std::function<void(float *stereoOut, uint32_t frameCount)> cb) {
            _playbackCallback = std::move(cb);
        }

      private:
        static void OnCapture(ma_device *device, void *output, const void *input, uint32_t frameCount);
        static void OnPlayback(ma_device *device, void *output, const void *input, uint32_t frameCount);

        ma_device *_captureDevice  = nullptr;
        ma_device *_playbackDevice = nullptr;
        bool _running              = false;

        std::function<void(float *, uint32_t)> _playbackCallback;

        // ~340ms of mono capture at 48kHz. Large enough to ride out a main-loop hitch,
        // small enough that a stalled consumer does not accumulate seconds of stale speech.
        SpscRing<int16_t, 16384> _captureRing;
    };
} // namespace Framework::Voice
```

- [ ] **Step 2: Write the implementation**

Create `code/framework/src/voice/client/audio_device.cpp`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "audio_device.h"

#include <logging/logger.h>

// The single translation unit that compiles miniaudio's implementation.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#include <cstring>
#include <new>

namespace Framework::Voice {
    AudioDevice::~AudioDevice() {
        Shutdown();
    }

    void AudioDevice::OnCapture(ma_device *device, void *, const void *input, uint32_t frameCount) {
        auto *self = static_cast<AudioDevice *>(device->pUserData);
        if (self == nullptr || input == nullptr) {
            return;
        }

        // A failed push means the main thread has fallen behind; dropping the newest audio
        // is correct here, because blocking or growing the buffer would turn a hitch into a
        // permanent latency increase.
        self->_captureRing.Push(static_cast<const int16_t *>(input), frameCount);
    }

    void AudioDevice::OnPlayback(ma_device *device, void *output, const void *, uint32_t frameCount) {
        auto *self = static_cast<AudioDevice *>(device->pUserData);
        if (self == nullptr) {
            return;
        }

        auto *out = static_cast<float *>(output);
        std::memset(out, 0, sizeof(float) * frameCount * 2);

        if (self->_playbackCallback) {
            self->_playbackCallback(out, frameCount);
        }
    }

    bool AudioDevice::Init() {
        if (_running) {
            return true;
        }

        _captureDevice  = new (std::nothrow) ma_device {};
        _playbackDevice = new (std::nothrow) ma_device {};
        if (_captureDevice == nullptr || _playbackDevice == nullptr) {
            Shutdown();
            return false;
        }

        ma_device_config captureConfig  = ma_device_config_init(ma_device_type_capture);
        captureConfig.capture.format    = ma_format_s16;
        captureConfig.capture.channels  = kChannels;
        captureConfig.sampleRate        = kSampleRate;
        captureConfig.periodSizeInFrames = kFrameSamples;
        captureConfig.dataCallback      = &AudioDevice::OnCapture;
        captureConfig.pUserData         = this;

        if (ma_device_init(nullptr, &captureConfig, _captureDevice) != MA_SUCCESS) {
            Framework::Logging::GetLogger("Voice")->warn("Failed to open capture device; voice input disabled");
            Shutdown();
            return false;
        }

        ma_device_config playbackConfig   = ma_device_config_init(ma_device_type_playback);
        playbackConfig.playback.format    = ma_format_f32;
        playbackConfig.playback.channels  = 2; // mixed to stereo for positional panning
        playbackConfig.sampleRate         = kSampleRate;
        playbackConfig.periodSizeInFrames = kFrameSamples;
        playbackConfig.dataCallback       = &AudioDevice::OnPlayback;
        playbackConfig.pUserData          = this;

        if (ma_device_init(nullptr, &playbackConfig, _playbackDevice) != MA_SUCCESS) {
            Framework::Logging::GetLogger("Voice")->warn("Failed to open playback device; voice output disabled");
            Shutdown();
            return false;
        }

        if (ma_device_start(_captureDevice) != MA_SUCCESS || ma_device_start(_playbackDevice) != MA_SUCCESS) {
            Framework::Logging::GetLogger("Voice")->warn("Failed to start audio devices");
            Shutdown();
            return false;
        }

        _running = true;
        Framework::Logging::GetLogger("Voice")->info("Audio devices started at {}Hz", kSampleRate);
        return true;
    }

    void AudioDevice::Shutdown() {
        if (_captureDevice != nullptr) {
            ma_device_uninit(_captureDevice);
            delete _captureDevice;
            _captureDevice = nullptr;
        }
        if (_playbackDevice != nullptr) {
            ma_device_uninit(_playbackDevice);
            delete _playbackDevice;
            _playbackDevice = nullptr;
        }
        _running = false;
    }

    bool AudioDevice::PopCapturedFrame(int16_t *out) {
        return _captureRing.Pop(out, kFrameSamples);
    }
} // namespace Framework::Voice
```

- [ ] **Step 3: Add the source and link miniaudio**

In `code/framework/CMakeLists.txt`, append to `FRAMEWORK_CLIENT_SRC`:

```cmake
    src/voice/client/audio_device.cpp
```

and add `miniaudio` to the `FrameworkClient` target's `target_link_libraries` call.

- [ ] **Step 4: Build**

Windows: `builds\build.bat FrameworkClient 64`
macOS/Linux: `cmake --build build --target FrameworkClient`

Expected: builds clean. miniaudio's implementation compiles only in `audio_device.cpp`; a duplicate-symbol error means `MINIAUDIO_IMPLEMENTATION` was defined in a second translation unit.

- [ ] **Step 5: Verify capture and playback against real hardware**

Automated tests cannot cover this. Write a temporary scratch program that initialises an `AudioDevice`, installs a playback callback that echoes whatever `PopCapturedFrame` returns, and runs for ten seconds.

```cpp
// scratch/audio_loopback.cpp — delete after verifying, do not commit
#include "voice/client/audio_device.h"
#include <chrono>
#include <thread>

int main() {
    Framework::Voice::AudioDevice device;
    static int16_t frame[Framework::Voice::kFrameSamples];
    static bool hasFrame = false;

    device.SetPlaybackCallback([&](float *out, uint32_t frames) {
        if (!hasFrame) return;
        for (uint32_t i = 0; i < frames && i < Framework::Voice::kFrameSamples; i++) {
            const float s = frame[i] / 32768.0f;
            out[i * 2] = s;
            out[i * 2 + 1] = s;
        }
    });

    if (!device.Init()) return 1;

    for (int i = 0; i < 500; i++) {
        hasFrame = device.PopCapturedFrame(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return 0;
}
```

Expected: speaking into the microphone is audible through the speakers with roughly 20–40ms of delay. Use headphones — speakers will feed back. Delete the scratch file afterwards.

- [ ] **Step 6: Commit**

```bash
scripts/format_codebase.sh
git add code/framework/src/voice/client/audio_device.h \
        code/framework/src/voice/client/audio_device.cpp code/framework/CMakeLists.txt
git commit -m "Voice: add miniaudio capture and playback device"
```

---

## Task 8: Server voice wiring

Connect the router to the network: attach `RakVoice` in relay mode, keep the position map fresh, and forward incoming frames.

**Files:**
- Create: `code/framework/src/voice/server/voice_server.h`, `code/framework/src/voice/server/voice_server.cpp`
- Modify: `code/framework/CMakeLists.txt`, `code/framework/src/core_modules.h`, `code/framework/src/integrations/server/instance.cpp`

**Interfaces:**
- Consumes: `VoiceRouter` (Task 4), `RakVoice::SetRelayHost` / `RelayFrame` / `ReadRelayOrigin` (Task 2), `kRecipientRefreshMs` (Task 3).

> **Changed after this plan was written — the code below is not literally what shipped:**
>
> - **The server-side audible-talker cap is gone.** `VoiceRouter::ComputeRecipients` used
>   to `partial_sort` and truncate to `kMaxAudibleTalkers` nearest recipients. That capped
>   how many *listeners a talker reaches* — the opposite of the intended per-listener
>   bound — and silently muted a talker to everyone past the sixth nearest person. It was
>   removed; the router now returns every eligible listener in range, in unspecified
>   order, and proximity is the only fan-out bound. `kMaxAudibleTalkers` now means only
>   the client mixer's speaker-slot count (see Task 9).
> - **The relay header gained a format-version byte.** Layout is now
>   `[id][format version = 1][origin guid][channel id][sequence][opus payload]`, with
>   `RAKVOICE_RELAY_OFFSET_VERSION` added and every later offset plus
>   `RAKVOICE_RELAY_HEADER_SIZE` derived from it in `RakVoice.h`. Never hand-edit an
>   offset. Both readers (`ReadRelayOrigin`, `OnRelayVoiceData`) reject a frame whose
>   version byte is not 1, silently, before touching any other field.
> - **`OnVoiceFrame` bounds frame size on both ends**, rejecting
>   `length > RAKVOICE_RELAY_HEADER_SIZE + RAKVOICE_MAX_OPUS_PACKET_SIZE` as well as the
>   header-only case, so a modified client cannot get an oversized payload amplified
>   across the whole proximity set.
> - **The server sets `SetRelayHost(true)` only**, never `SetRelayMode(true)`.
- Produces:
  - `Framework::Voice::VoiceServer` with `bool Init(Networking::NetworkServer *server)`, `void Update()`, `void Shutdown()`, `VoiceRouter &GetRouter()`
  - `CoreModules::SetVoiceServer(Voice::VoiceServer *)` / `CoreModules::GetVoiceServer()`

- [ ] **Step 1: Write the header**

Create `code/framework/src/voice/server/voice_server.h`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice_router.h"

#include <mafianet/RakVoice.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Framework::Networking {
    class NetworkServer;
} // namespace Framework::Networking

namespace Framework::Voice {
    // Server half of voice: owns the routing rule and forwards frames. Deliberately never
    // initialises a codec — RakVoice is attached purely so its relay path can forward
    // payloads, which is what keeps voice off the server's CPU budget.
    class VoiceServer final {
      public:
        bool Init(Networking::NetworkServer *server);
        void Shutdown();

        // Call once per server tick. Refreshes cached recipient sets on the configured
        // interval; frame forwarding itself happens on packet arrival, not here.
        void Update();

        VoiceRouter &GetRouter() {
            return _router;
        }

        // Called by the network layer for every ID_RAKVOICE_RELAY_DATA packet.
        void OnVoiceFrame(MafiaNet::Packet *packet);

        void OnPlayerDisconnect(uint64_t guid);

      private:
        // Recipient sets are cached per talker and refreshed on an interval rather than per
        // frame; see kRecipientRefreshMs.
        struct CachedRecipients {
            std::vector<MafiaNet::RakNetGUID> guids;
            uint64_t computedAtMs = 0;
        };

        const std::vector<MafiaNet::RakNetGUID> &RecipientsFor(uint64_t talker);

        Networking::NetworkServer *_server = nullptr;
        MafiaNet::RakVoice _voice;
        VoiceRouter _router;
        std::unordered_map<uint64_t, CachedRecipients> _cache;
        std::vector<uint64_t> _scratch;
    };
} // namespace Framework::Voice
```

- [ ] **Step 2: Write the implementation**

Create `code/framework/src/voice/server/voice_server.cpp`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "voice_server.h"

#include "voice/voice_config.h"

#include <logging/logger.h>
#include <networking/network_server.h>
#include <utils/time.h>

namespace Framework::Voice {
    bool VoiceServer::Init(Networking::NetworkServer *server) {
        if (server == nullptr || server->GetPeer() == nullptr) {
            return false;
        }

        _server = server;

        // Relay HOST only: no Init() call, so no encoder or decoder is ever allocated here.
        // SetRelayHost is mandatory — without it RakVoice swallows ID_RAKVOICE_RELAY_DATA
        // inside RakPeer::Receive and OnVoiceFrame never fires.
        //
        // SetRelayMode is deliberately NOT set here. It is the *client* flag: it opens a
        // self-keyed encoder channel and drives the relay reap branch in Update(). A host
        // neither encodes nor decodes, and RelayFrame ignores relayMode entirely, so
        // setting it would only put the plugin in a contradictory state. (An earlier
        // revision of this plan called SetRelayMode(true) here; shipped code does not.)
        _voice.SetRelayHost(true);
        server->GetPeer()->AttachPlugin(&_voice);

        // Debug, not info: until the client half lands, nothing can produce a frame.
        Framework::Logging::GetLogger("Voice")->debug("Voice relay attached");
        return true;
    }

    void VoiceServer::Shutdown() {
        if (_server != nullptr && _server->GetPeer() != nullptr) {
            _server->GetPeer()->DetachPlugin(&_voice);
        }
        _server = nullptr;
        _cache.clear();
    }

    void VoiceServer::Update() {
        // Nothing periodic is required: cache entries expire lazily in RecipientsFor().
        // Kept as an explicit hook so M2's channel bookkeeping has somewhere to live.
    }

    void VoiceServer::OnPlayerDisconnect(uint64_t guid) {
        _router.RemovePlayer(guid);
        _cache.erase(guid);
    }

    const std::vector<MafiaNet::RakNetGUID> &VoiceServer::RecipientsFor(uint64_t talker) {
        auto &entry           = _cache[talker];
        const uint64_t nowMs  = Framework::Utils::Time::GetTimeMillis();

        if (nowMs - entry.computedAtMs < kRecipientRefreshMs && entry.computedAtMs != 0) {
            return entry.guids;
        }

        _router.ComputeRecipients(talker, _scratch);

        entry.guids.clear();
        entry.guids.reserve(_scratch.size());
        for (const uint64_t guid : _scratch) {
            MafiaNet::RakNetGUID converted;
            converted.g = guid;
            entry.guids.push_back(converted);
        }
        entry.computedAtMs = nowMs;

        return entry.guids;
    }

    void VoiceServer::OnVoiceFrame(MafiaNet::Packet *packet) {
        const MafiaNet::RakNetGUID origin = MafiaNet::RakVoice::ReadRelayOrigin(packet);
        if (origin == UNASSIGNED_RAKNET_GUID) {
            return;
        }

        // A client may only speak as itself. Without this check a modified client could
        // stamp someone else's GUID and impersonate them.
        if (origin != packet->guid) {
            return;
        }

        const auto &recipients = RecipientsFor(origin.g);
        if (recipients.empty()) {
            return;
        }

        _voice.RelayFrame(packet, recipients.data(), static_cast<int>(recipients.size()));
    }
} // namespace Framework::Voice
```

- [ ] **Step 3: Register in CoreModules**

In `code/framework/src/core_modules.h`, add the forward declaration beside the others:

```cpp
namespace Framework::Voice {
    class VoiceServer;
} // namespace Framework::Voice
```

a setter beside the existing ones:

```cpp
        static void SetVoiceServer(Voice::VoiceServer *voice) {
            FW_ASSERT_MODULE_REGISTRATION(_voiceServer, voice, "VoiceServer");
            _voiceServer = voice;
        }
```

a getter:

```cpp
        static Voice::VoiceServer *GetVoiceServer() noexcept {
            return _voiceServer;
        }
```

the static member alongside the others, and `_voiceServer = nullptr;` inside `Reset()`.

- [ ] **Step 4: Route the packet and the lifecycle from the server instance**

In `code/framework/src/integrations/server/instance.cpp`:

- construct a `Voice::VoiceServer` member and call `Init(GetNetworkingEngine()->GetNetworkServer())` after networking comes up, then `CoreModules::SetVoiceServer(&_voiceServer)`;
- register an unknown-packet handler branch for `ID_RAKVOICE_RELAY_DATA` that calls `_voiceServer.OnVoiceFrame(packet)`;
- in the per-tick update, push each connected player's position into the router:

```cpp
        auto &router = _voiceServer.GetRouter();
        _replicationManager->ForEach<Framework::Networking::Replication::NetworkEntity>(
            [&](Framework::Networking::Replication::NetworkEntity *entity) {
                if (entity->GetOwnerGUID() != UNASSIGNED_RAKNET_GUID) {
                    router.SetPlayerPosition(entity->GetOwnerGUID().g, entity->GetPosition());
                }
            });
```

- in `OnPlayerDisconnect`, call `_voiceServer.OnPlayerDisconnect(guid.g)`;
- in shutdown, call `_voiceServer.Shutdown()`.

- [ ] **Step 5: Add the source to the build**

In `code/framework/CMakeLists.txt`, append to `FRAMEWORK_SERVER_SRC`:

```cmake
    src/voice/server/voice_server.cpp
```

- [ ] **Step 6: Build and verify the existing tests still pass**

Windows: `builds\build.bat FrameworkServer 64`
macOS/Linux: `cmake --build build --target FrameworkServer && cmake --build build --target RunFrameworkTests`

Expected: builds clean and all previously passing test modules still pass.

- [ ] **Step 7: Commit**

```bash
scripts/format_codebase.sh
git add code/framework/src/voice/server code/framework/src/core_modules.h \
        code/framework/src/integrations/server/instance.cpp code/framework/CMakeLists.txt
git commit -m "Voice: wire proximity relay into the server instance"
```

---

## Task 9: Client voice pipeline

The piece that makes sound come out: capture, gate on push-to-talk, encode, send, receive per speaker, position, mix.

> **Carry-notes from the server half — read before starting:**
>
> - **`SetLoopbackMode` is a no-op in relay mode.** `OnRelayVoiceData` drops any frame
>   whose origin GUID equals our own, which is exactly what a loopback frame is. This
>   invalidates **Step 8's loopback verification as written** — holding push-to-talk with
>   loopback on will produce silence, and that is not evidence of a bug in your pipeline.
>   Either add a self-origin exemption to `OnRelayVoiceData` gated on `loopbackMode`, or
>   verify the roundtrip with a relay-aware path (two peers, or a stub host that echoes
>   the frame back). Decide which before writing Step 8.
> - **`kMaxSpeakerSlots = kMaxAudibleTalkers` is now the only enforcement of the
>   per-listener bound.** The server-side cap was removed (see the note in Task 8), so the
>   router will happily send a listener every talker in range. The slot array below is
>   what keeps decode cost bounded; sizing it, and choosing which speaker gets evicted
>   when all slots are full, is now a real decision rather than a redundant safety net.
>   It bounds decode, not inbound bandwidth — a true bandwidth bound is M2.
> - **Set the encoder bitrate explicitly.** `RakVoice::SetEncoderBitrate(kBitrate)` was
>   added for this; without it Opus picks its own rate and the spec's 24kbps figure is
>   fiction. Call it after `Init()`, before speaking.
> - **RNNoise does not run** at `kFrameSamples` (960). M1 ships undenoised by decision;
>   see the design spec's Constraints section. Do not "fix" it by changing the frame size.

**Files:**
- Create: `code/framework/src/voice/client/i_voice_sink.h`, `code/framework/src/voice/client/voice_client.h`, `code/framework/src/voice/client/voice_client.cpp`
- Modify: `code/framework/CMakeLists.txt`, `code/framework/src/core_modules.h`, `code/framework/src/integrations/client/instance.cpp`

**Interfaces:**
- Consumes: `AudioDevice` (Task 7), `ComputeGain`/`MixFrameInto` (Task 6), `SpscRing` (Task 5), `RakVoice` relay API (Task 2).
- Produces:
  - `Framework::Voice::IVoiceSink`
  - `Framework::Voice::VoiceClient` with `bool Init(Networking::NetworkClient *)`, `void Update()`, `void Shutdown()`, `void SetPushToTalk(bool)`, `void SetListenerTransform(const ListenerTransform &)`, `void SetSpeakerPosition(uint64_t, const glm::vec3 &)`, `void SetLocalMuted(uint64_t, bool)`, `void SetOutputVolume(float)`, `bool IsSpeaking(uint64_t) const`
  - `CoreModules::SetVoiceClient()` / `GetVoiceClient()`

- [ ] **Step 1: Define the sink interface**

Create `code/framework/src/voice/client/i_voice_sink.h`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Framework::Voice {
    // Where decoded voice goes. The default implementation mixes into the framework's own
    // miniaudio stream; a project that wants voice to inherit its game engine's occlusion
    // and reverb implements this instead, without touching transport, codec or scripting.
    class IVoiceSink {
      public:
        virtual ~IVoiceSink() = default;

        // One 20ms mono frame from `speaker`, already decoded. `samples` is kFrameSamples.
        // May be called from an audio thread: no allocation, no locking.
        virtual void Submit(uint64_t speaker, const int16_t *pcm, uint32_t samples, const glm::vec3 &position) = 0;

        // A speaker stopped talking or left; release any per-speaker state.
        virtual void ReleaseSpeaker(uint64_t speaker) = 0;
    };
} // namespace Framework::Voice
```

- [ ] **Step 2: Write the client header**

Create `code/framework/src/voice/client/voice_client.h`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "audio_device.h"
#include "i_voice_sink.h"
#include "mixer.h"
#include "spsc_ring.h"
#include "voice/voice_config.h"

#include <mafianet/RakVoice.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace Framework::Networking {
    class NetworkClient;
} // namespace Framework::Networking

namespace Framework::Voice {
    // Client half of voice. Owns the audio device, the codec, and one PCM queue per remote
    // speaker.
    //
    // Threading: capture and playback run on miniaudio's threads; RakVoice is touched only
    // from the main thread in Update(). Decoded frames cross to the playback thread through
    // per-speaker lock-free queues, and positions cross through an atomically published
    // snapshot — never as shared mutable state.
    class VoiceClient final {
      public:
        bool Init(Networking::NetworkClient *client);
        void Shutdown();

        // Main thread, once per frame: drains captured audio into the encoder and decoded
        // audio into the per-speaker queues.
        void Update();

        // Opens or closes the microphone gate. Bound to a key by the game.
        void SetPushToTalk(bool active) {
            _pushToTalk.store(active, std::memory_order_relaxed);
        }

        // Where the local player hears from. Published each frame by the game.
        void SetListenerTransform(const ListenerTransform &transform);

        // Where a remote speaker is in the world. Published each frame by the game.
        void SetSpeakerPosition(uint64_t speaker, const glm::vec3 &position);

        // Local playback mute for one remote speaker. Does not affect other listeners.
        void SetLocalMuted(uint64_t speaker, bool muted);

        void SetOutputVolume(float volume) {
            _outputVolume.store(volume, std::memory_order_relaxed);
        }

        // True if that speaker delivered a frame recently. Drives the speaking indicator.
        bool IsSpeaking(uint64_t speaker) const;

        // Replaces the default miniaudio sink. Pass nullptr to restore the default.
        void SetSink(IVoiceSink *sink) {
            _sink = sink;
        }

      private:
        // Per-speaker playback state. The queue is written on the main thread and read on
        // the playback thread; position and mute are atomics for the same reason.
        //
        // Slots live in a fixed-size array, NOT a map. The playback thread scans this array
        // concurrently with the main thread adding and reaping speakers, and iterating a
        // std::unordered_map while another thread inserts or erases is undefined behaviour
        // — rehashing would invalidate the audio thread's iterator mid-callback. A fixed
        // array has stable addresses, needs no allocation, and lets a slot be claimed or
        // released with a single atomic store on `id`.
        struct SpeakerSlot {
            std::atomic<uint64_t> id {0}; // 0 means free
            SpscRing<int16_t, 8192> pcm;  // ~170ms of jitter headroom
            std::atomic<float> posX {0.0f};
            std::atomic<float> posY {0.0f};
            std::atomic<float> posZ {0.0f};
            std::atomic<bool> muted {false};
            std::atomic<uint64_t> lastFrameMs {0};
        };

        // Matches the server's per-listener audible-talker cap: the server never forwards
        // more than this many concurrent talkers to one client.
        static constexpr size_t kMaxSpeakerSlots = kMaxAudibleTalkers;

        void OnPlaybackNeeded(float *stereoOut, uint32_t frameCount);

        // Main thread only. Returns the slot for `id`, claiming a free one if needed, or
        // nullptr when every slot is occupied.
        SpeakerSlot *AcquireSlot(uint64_t id);

        Networking::NetworkClient *_client = nullptr;
        MafiaNet::RakVoice _voice;
        AudioDevice _device;
        IVoiceSink *_sink = nullptr;

        std::array<SpeakerSlot, kMaxSpeakerSlots> _speakers;

        std::atomic<bool> _pushToTalk {false};
        std::atomic<float> _outputVolume {1.0f};

        // Listener transform double-buffered: the main thread writes the inactive slot then
        // flips the index, so the playback thread always reads a complete transform.
        ListenerTransform _listener[2];
        std::atomic<uint32_t> _listenerIndex {0};

        int16_t _captureScratch[kFrameSamples] {};
        int16_t _decodeScratch[kFrameSamples] {};
    };
} // namespace Framework::Voice
```

- [ ] **Step 3: Write the client implementation**

Create `code/framework/src/voice/client/voice_client.cpp`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "voice_client.h"

#include <logging/logger.h>
#include <networking/network_client.h>
#include <utils/time.h>

#include <mafianet/DS_List.h>

namespace Framework::Voice {
    bool VoiceClient::Init(Networking::NetworkClient *client) {
        if (client == nullptr || client->GetPeer() == nullptr) {
            return false;
        }

        _client = client;

        _voice.SetRelayMode(true);
        _voice.SetPerSpeakerOutput(true);
        client->GetPeer()->AttachPlugin(&_voice);

        _voice.Init(static_cast<unsigned short>(kSampleRate), kFrameBytes);
        _voice.SetVAD(true);
        _voice.SetVBR(true);
        _voice.SetNoiseFilter(true);

        _device.SetPlaybackCallback([this](float *out, uint32_t frames) {
            OnPlaybackNeeded(out, frames);
        });

        // Voice is optional: a missing microphone must not stop the client from running.
        if (!_device.Init()) {
            Framework::Logging::GetLogger("Voice")->warn("Voice disabled: no usable audio device");
            return false;
        }

        return true;
    }

    void VoiceClient::Shutdown() {
        _device.Shutdown();

        if (_client != nullptr && _client->GetPeer() != nullptr) {
            _client->GetPeer()->DetachPlugin(&_voice);
        }
        _voice.Deinit();

        // Devices are already stopped, so no audio thread is scanning the slots.
        for (auto &slot : _speakers) {
            slot.id.store(0, std::memory_order_relaxed);
            slot.pcm.Clear();
        }

        _client = nullptr;
    }

    void VoiceClient::SetListenerTransform(const ListenerTransform &transform) {
        const uint32_t next = 1u - _listenerIndex.load(std::memory_order_relaxed);
        _listener[next]     = transform;
        _listenerIndex.store(next, std::memory_order_release);
    }

    VoiceClient::SpeakerSlot *VoiceClient::AcquireSlot(uint64_t id) {
        SpeakerSlot *free = nullptr;

        for (auto &slot : _speakers) {
            const uint64_t current = slot.id.load(std::memory_order_relaxed);
            if (current == id) {
                return &slot;
            }
            if (current == 0 && free == nullptr) {
                free = &slot;
            }
        }

        if (free == nullptr) {
            return nullptr; // every slot busy; the server caps talkers so this is rare
        }

        free->pcm.Clear();
        free->muted.store(false, std::memory_order_relaxed);
        free->lastFrameMs.store(0, std::memory_order_relaxed);

        // Published last: the audio thread treats a non-zero id as "this slot is readable",
        // so everything it will read must already be in place.
        free->id.store(id, std::memory_order_release);
        return free;
    }

    void VoiceClient::SetSpeakerPosition(uint64_t speaker, const glm::vec3 &position) {
        for (auto &slot : _speakers) {
            if (slot.id.load(std::memory_order_relaxed) != speaker) {
                continue;
            }
            slot.posX.store(position.x, std::memory_order_relaxed);
            slot.posY.store(position.y, std::memory_order_relaxed);
            slot.posZ.store(position.z, std::memory_order_relaxed);
            return;
        }
    }

    void VoiceClient::SetLocalMuted(uint64_t speaker, bool muted) {
        for (auto &slot : _speakers) {
            if (slot.id.load(std::memory_order_relaxed) == speaker) {
                slot.muted.store(muted, std::memory_order_relaxed);
                return;
            }
        }
    }

    bool VoiceClient::IsSpeaking(uint64_t speaker) const {
        for (const auto &slot : _speakers) {
            if (slot.id.load(std::memory_order_relaxed) != speaker) {
                continue;
            }
            const uint64_t last = slot.lastFrameMs.load(std::memory_order_relaxed);
            return last != 0 && (Framework::Utils::Time::GetTimeMillis() - last) < 300;
        }
        return false;
    }

    void VoiceClient::Update() {
        if (!_device.IsRunning() || _client == nullptr) {
            return;
        }

        _voice.SetRelayTarget(_client->GetPeer()->GetGuidFromSystemAddress(
            _client->GetPeer()->GetSystemAddressFromIndex(0)));

        // Capture -> encoder. RakVoice's DTX suppresses silence on the wire, but gating on
        // push-to-talk here means an un-keyed microphone is never encoded at all.
        if (_pushToTalk.load(std::memory_order_relaxed)) {
            while (_device.PopCapturedFrame(_captureScratch)) {
                // In relay mode the GUID selects the *local encoder channel*, not a
                // destination — the destination is the relay target set above. See
                // RakVoice::SetRelayMode.
                _voice.SendFrame(_voice.GetRakPeerInterface()->GetMyGUID(), _captureScratch);
            }
        }

        // Decoder -> per-speaker queues. RakVoice must only ever be touched from here.
        DataStructures::List<MafiaNet::RakNetGUID> speakers;
        _voice.GetActiveSpeakers(speakers);

        const uint64_t nowMs = Framework::Utils::Time::GetTimeMillis();

        for (unsigned i = 0; i < speakers.Size(); i++) {
            SpeakerSlot *slot = AcquireSlot(speakers[i].g);
            if (slot == nullptr) {
                continue; // no free slot; drop this talker rather than stall the tick
            }

            while (_voice.ReceiveFrameFrom(speakers[i], _decodeScratch)) {
                slot->pcm.Push(_decodeScratch, kFrameSamples);
                slot->lastFrameMs.store(nowMs, std::memory_order_relaxed);
            }
        }

        // Release slots that have gone quiet and drained, so a long session does not hold
        // every player who ever spoke. Clearing `id` last is what makes this safe against
        // the audio thread: once it reads 0 it stops touching the slot entirely.
        for (auto &slot : _speakers) {
            const uint64_t id = slot.id.load(std::memory_order_relaxed);
            if (id == 0) {
                continue;
            }

            const uint64_t last = slot.lastFrameMs.load(std::memory_order_relaxed);
            if (last != 0 && (nowMs - last) > kSpeakerSilenceTimeoutMs && slot.pcm.Available() == 0) {
                if (_sink != nullptr) {
                    _sink->ReleaseSpeaker(id);
                }
                slot.id.store(0, std::memory_order_release);
            }
        }
    }

    void VoiceClient::OnPlaybackNeeded(float *stereoOut, uint32_t frameCount) {
        // Audio thread. No allocation, no locking, no MafiaNet calls.
        const ListenerTransform &listener = _listener[_listenerIndex.load(std::memory_order_acquire)];
        const float masterVolume          = _outputVolume.load(std::memory_order_relaxed);

        // Scanning a fixed array, so the main thread claiming or releasing a slot underneath
        // is safe: it only ever flips `id`, and a slot that reads 0 is skipped.
        for (auto &slot : _speakers) {
            if (slot.id.load(std::memory_order_acquire) == 0) {
                continue;
            }
            if (slot.muted.load(std::memory_order_relaxed)) {
                continue;
            }
            if (slot.pcm.Available() < frameCount) {
                continue; // underrun: leave silence rather than stutter a partial frame
            }

            static thread_local int16_t mono[kFrameSamples];
            const uint32_t take = frameCount < kFrameSamples ? frameCount : kFrameSamples;
            if (!slot.pcm.Pop(mono, take)) {
                continue;
            }

            const glm::vec3 position(slot.posX.load(std::memory_order_relaxed),
                                     slot.posY.load(std::memory_order_relaxed),
                                     slot.posZ.load(std::memory_order_relaxed));

            SpeakerGain gain = ComputeGain(listener, position, kDefaultProximityRange);
            gain.left *= masterVolume;
            gain.right *= masterVolume;

            MixFrameInto(stereoOut, mono, take, gain);
        }
    }
} // namespace Framework::Voice
```

- [ ] **Step 4: Register in CoreModules**

In `code/framework/src/core_modules.h`, add `class VoiceClient;` to the `Framework::Voice` forward-declaration block, then a `SetVoiceClient`/`GetVoiceClient` pair, the `_voiceClient` static member, and `_voiceClient = nullptr;` in `Reset()`, matching the `VoiceServer` pattern from Task 8.

- [ ] **Step 5: Drive it from the client instance**

In `code/framework/src/integrations/client/instance.cpp`:

- add a `Voice::VoiceClient _voiceClient;` member;
- call `_voiceClient.Init(GetNetworkingEngine()->GetNetworkClient())` from `OnConnectionFinalized`, then `CoreModules::SetVoiceClient(&_voiceClient)`;
- call `_voiceClient.Update()` once per tick from `Update()`;
- call `_voiceClient.Shutdown()` from `OnConnectionClosed` and from shutdown.

- [ ] **Step 6: Add the source to the build**

In `code/framework/CMakeLists.txt`, append to `FRAMEWORK_CLIENT_SRC`:

```cmake
    src/voice/client/voice_client.cpp
```

- [ ] **Step 7: Build**

Windows: `builds\build.bat FrameworkClient 64`
macOS/Linux: `cmake --build build --target FrameworkClient`

Expected: builds clean.

- [ ] **Step 8: Verify the codec roundtrip with loopback**

`RakVoice::SetLoopbackMode(true)` routes encoded audio back to the sender, which exercises encode and decode without a second machine. In a temporary scratch program, init a `VoiceClient` against a locally connected peer, call `_voice.SetLoopbackMode(true)`, hold push-to-talk, and confirm your own voice returns through the mixer.

Expected: your own speech is audible with roughly 40–60ms of delay. Silence means the encode or decode path is broken; check that `SetPerSpeakerOutput(true)` did not suppress a frame the loopback path needs.

- [ ] **Step 9: Commit**

```bash
scripts/format_codebase.sh
git add code/framework/src/voice/client code/framework/src/core_modules.h \
        code/framework/src/integrations/client/instance.cpp code/framework/CMakeLists.txt
git commit -m "Voice: add client capture, decode and 3D playback pipeline"
```

---

## Task 10: M2O integration

Feed the Framework the two things only the game knows: where the listener is, and where everyone else is.

**Files:**
- Create: `code/projects/m2o/code/client/src/core/modules/voice.h`, `code/projects/m2o/code/client/src/core/modules/voice.cpp`
- Modify: `code/projects/m2o/code/client/CMakeLists.txt`, `code/projects/m2o/code/client/src/core/application.cpp`

**Interfaces:**
- Consumes: `VoiceClient::SetListenerTransform`, `SetSpeakerPosition`, `SetPushToTalk`, `IsSpeaking` (Task 9).
- Produces: `M2O::Core::Modules::Voice::Update()`, `M2O::Core::Modules::Voice::Init()`.

- [ ] **Step 1: Write the module header**

Create `code/projects/m2o/code/client/src/core/modules/voice.h`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace M2O::Core::Modules::Voice {
    // Per-frame: publishes the camera as the listener transform, every streamed human's
    // position as a speaker position, and the push-to-talk key state. Voice positioning is
    // only as fresh as this call.
    //
    // There is no Init(): everything here is polled, and the VoiceClient itself is brought
    // up by the framework's client instance. Key rebinding arrives in M3 with the settings
    // UI.
    void Update();
} // namespace M2O::Core::Modules::Voice
```

- [ ] **Step 2: Write the module implementation**

Create `code/projects/m2o/code/client/src/core/modules/voice.cpp`:

```cpp
/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "voice.h"

#include "../application.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <voice/client/voice_client.h>

#include <shared/entities/human.h>

namespace M2O::Core::Modules::Voice {
    namespace {
        // Mafia 2 is Z-up, so the listener's up vector is +Z rather than the +Y the mixer
        // defaults to. Getting this wrong makes the left/right pan collapse.
        constexpr glm::vec3 kUpAxis {0.0f, 0.0f, 1.0f};

        Framework::Voice::VoiceClient *Client() {
            return Framework::CoreModules::GetVoiceClient();
        }
    } // namespace

    void Update() {
        auto *voice = Client();
        if (voice == nullptr || Core::gApplication == nullptr) {
            return;
        }

        // Listener follows the camera, not the player body: a player in a cutscene or a
        // rotated camera should hear from where they are looking.
        const auto *camera = Core::gApplication->GetCamera();
        if (camera != nullptr) {
            Framework::Voice::ListenerTransform listener;
            listener.position = camera->GetPosition();
            listener.forward  = camera->GetForward();
            listener.up       = kUpAxis;
            voice->SetListenerTransform(listener);
        }

        // Speaker positions for every streamed remote human.
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication != nullptr) {
            replication->ForEach<Shared::Entities::HumanEntity>([&](Shared::Entities::HumanEntity *human) {
                voice->SetSpeakerPosition(human->GetOwnerGUID().g, human->GetPosition());
            });
        }

        // Push-to-talk gate. Held, not toggled.
        auto *input = Framework::CoreModules::GetInput();
        if (input != nullptr) {
            voice->SetPushToTalk(input->IsKeyDown(VK_CAPITAL));
        }
    }
} // namespace M2O::Core::Modules::Voice
```

The three game-side accessors above (`GetCamera()`, `HumanEntity`, `IsKeyDown`) are written from the established patterns but were not verified against the M2O tree while planning. Confirm each before compiling:

```bash
grep -rn "GetCamera\|C_Camera" code/projects/m2o/code/client/src/core/application.h
grep -rn "class .*Entity" code/projects/m2o/code/shared/entities/human.h
grep -rn "IsKeyDown\|IsKeyPressed" code/framework/src/input/
```

Use whatever those report; the shape of the module does not change.

- [ ] **Step 3: Add the source to the M2O build**

In `code/projects/m2o/code/client/CMakeLists.txt`, add `src/core/modules/voice.cpp` to the client source list.

- [ ] **Step 4: Call the module from the application loop**

In `code/projects/m2o/code/client/src/core/application.cpp`, call `Modules::Voice::Update()` from `PostUpdate()`, beside the other per-frame module updates.

- [ ] **Step 5: Build**

Windows: `builds\build.bat M2OClient 64`

Expected: builds clean.

- [ ] **Step 6: Verify end to end with two clients**

Start an M2O server on localhost and connect two clients (two machines, or one machine with headphones on each).

Check each of these:

1. Client A holds Caps Lock and speaks → client B hears it. Releasing the key stops audio within one frame.
2. B walks away from A. Volume falls with distance and goes fully silent past 25 units.
3. B stands to A's left → A's voice is louder in B's right ear. Walking around B swings the pan smoothly with no dropout as they cross directly in front.
4. Neither client hears its own voice.
5. Three clients: two talking at once are both audible and neither clips.
6. Client A disconnects mid-sentence → B hears no stuck loop or repeated tail.

- [ ] **Step 7: Commit**

```bash
scripts/format_codebase.sh
git add code/projects/m2o/code/client/src/core/modules/voice.h \
        code/projects/m2o/code/client/src/core/modules/voice.cpp \
        code/projects/m2o/code/client/CMakeLists.txt \
        code/projects/m2o/code/client/src/core/application.cpp
git commit -m "M2O: add proximity voice chat client module"
```

---

## Verification Summary

| Layer | How it is verified |
|---|---|
| `VoiceRouter` | 9 unit tests (Task 4) |
| `SpscRing` | 6 unit tests (Task 5) |
| `Mixer` | 8 unit tests (Task 6) |
| `RakVoice` relay | Compile + loopback (Task 9 Step 8) + two-client run (Task 10 Step 6) |
| `AudioDevice` | Hardware loopback scratch program (Task 7 Step 5) |
| End to end | Six-point two-client checklist (Task 10 Step 6) |

The three pure components carry real automated coverage. The device and codec layers cannot be unit-tested without hardware and a live peer, so they are covered by explicit manual procedures rather than left unverified.

## Known Local Constraint

`FrameworkClient` is declared inside a `WIN32` guard (`code/framework/CMakeLists.txt:144-172`), so **no client target exists on macOS or Linux** — neither `FrameworkClient` nor `M2OClient`.

- **Tasks 1–6 and 8** build and test on macOS. Verified baseline before any task: `RunFrameworkTests` passes 158 tests across 12 modules.
- **Tasks 7, 9 and 10** are client-side and can only be built, run, and hardware-verified on Windows via `builds\build.bat`.

Task 6 compiles `mixer.cpp` directly into `FrameworkTests` rather than linking `FrameworkClient`, which is what lets the mixer's unit tests run on every platform despite living in the client source list.
