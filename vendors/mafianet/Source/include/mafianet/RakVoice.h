/*
 *  Original work: Copyright (c) 2014, Oculus VR, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  RakNet License.txt file in the licenses directory of this source tree. An additional grant
 *  of patent rights can be found in the RakNet Patents.txt file in the same directory.
 *
 *
 *  Modified work: Copyright (c) 2017-2020, SLikeSoft UG (haftungsbeschränkt)
 *  Modified work: Copyright (c) 2024, MafiaHub - Opus migration
 *
 *  This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
 *  license found in the license.txt file in the root directory of this source tree.
 */

/// \file
/// \brief Voice compression and transmission interface using Opus codec

#ifndef __RAK_VOICE_H
#define __RAK_VOICE_H

#include "mafianet/types.h"
#include "mafianet/PluginInterface2.h"
#include "mafianet/DS_OrderedList.h"
#include "mafianet/NativeTypes.h"

// Forward declarations for Opus and RNNoise
struct OpusEncoder;
struct OpusDecoder;
struct DenoiseState;

namespace MafiaNet {

class RakPeerInterface;

// How many frames large to make the circular buffers in the VoiceChannel structure
#define FRAME_OUTGOING_BUFFER_COUNT 100
#define FRAME_INCOMING_BUFFER_COUNT 100

// Largest Opus packet we will ever produce or accept for a single frame.
// Exposed here (rather than only in RakVoice.cpp) so a relay host can bound an
// inbound frame without duplicating the literal.
constexpr unsigned RAKVOICE_MAX_OPUS_PACKET_SIZE = 4000;

// Wire layout of an ID_RAKVOICE_RELAY_DATA frame:
// [id][format version][origin guid][channel id][sequence][opus payload]
// Single source of truth for both the writer (Update) and the readers
// (ReadRelayOrigin, OnRelayVoiceData). Every offset is derived from the one before
// it, so a layout change is a single edit here.
constexpr unsigned char RAKVOICE_RELAY_FORMAT_VERSION = 1;

constexpr unsigned RAKVOICE_RELAY_OFFSET_VERSION = sizeof(unsigned char);
constexpr unsigned RAKVOICE_RELAY_OFFSET_ORIGIN = RAKVOICE_RELAY_OFFSET_VERSION + sizeof(uint8_t);
constexpr unsigned RAKVOICE_RELAY_OFFSET_CHANNEL_ID = RAKVOICE_RELAY_OFFSET_ORIGIN + sizeof(uint64_t);
constexpr unsigned RAKVOICE_RELAY_OFFSET_SEQUENCE = RAKVOICE_RELAY_OFFSET_CHANNEL_ID + sizeof(uint16_t);
constexpr unsigned RAKVOICE_RELAY_HEADER_SIZE = RAKVOICE_RELAY_OFFSET_SEQUENCE + sizeof(unsigned short);

// Codec-level backstop for reaping relay speakers we have stopped hearing from.
// Relay speakers are peers of the server, not of us, so OnClosedConnection never fires
// for them and nothing else would ever free their channels. Deliberately much longer
// than the speaker timeout the client layer applies on top, so the two do not fight.
constexpr MafiaNet::TimeMS RAKVOICE_RELAY_CHANNEL_TIMEOUT_MS = 30000;

/// \internal
struct VoiceChannel
{
	RakNetGUID guid;
	OpusEncoder *encoder;
	OpusDecoder *decoder;
	DenoiseState *denoiser;
	unsigned int remoteSampleRate;

	// Frame sizes for Opus
	int frameSizeSamples;
	int maxPacketBytes;

	// Circular buffer of unencoded sound data read from the user.
	char *outgoingBuffer;
	// Index in bytes.
	// Write index points to the next byte to write to, which must be free.
	unsigned outgoingReadIndex, outgoingWriteIndex;
	bool isSendingVoiceData;
	bool bufferOutput;
	bool copiedOutgoingBufferToBufferedOutput;
	unsigned short outgoingMessageNumber;

	// Circular buffer of unencoded sound data to be passed to the user.
	char *incomingBuffer;
	unsigned incomingReadIndex, incomingWriteIndex;
	unsigned short incomingMessageNumber;

	MafiaNet::TimeMS lastSend;
	// When we last decoded a frame into this channel. Drives the relay-mode reap in Update().
	MafiaNet::TimeMS lastDecode;
};
int VoiceChannelComp( const RakNetGUID &key, VoiceChannel * const &data );

/// Voice compression and transmission interface using Opus codec
class RAK_DLL_EXPORT RakVoice : public PluginInterface2
{
public:
	RakVoice();
	virtual ~RakVoice();

	// --------------------------------------------------------------------------------------------
	// User functions
	// --------------------------------------------------------------------------------------------

	/// \brief Starts RakVoice with Opus codec
	/// \param[in] sampleRate 8000, 16000, 24000, or 48000 (native Opus rates)
	/// \param[in] bufferSizeBytes How many bytes long inputBuffer and outputBuffer are in SendFrame and ReceiveFrame. Should be your sample size * the number of samples to encode at once.
	void Init(unsigned short sampleRate, unsigned bufferSizeBytes);

	/// \brief Enables or disables VAD (Voice Activity Detection) via Opus DTX
	/// Enabling VAD reduces bandwidth by not transmitting silence.
	/// \pre Only applies to encoder.
	/// \param[in] enable true to enable, false to disable. True by default
	void SetVAD(bool enable);

	/// \brief Enables or disables the RNNoise noise filter
	/// \pre Only applies to encoder.
	/// \param[in] enable true to enable, false to disable.
	void SetNoiseFilter(bool enable);

	/// \brief Enables or disables VBR (Variable Bitrate)
	/// VBR uses less bandwidth but more CPU if on.
	/// \pre Only applies to encoder.
	/// \param[in] enable true to enable VBR, false to disable
	void SetVBR(bool enable);

	/// \brief Sets the signal type hint for Opus encoder
	/// \param[in] signalType OPUS_SIGNAL_VOICE (default) or OPUS_SIGNAL_MUSIC
	void SetSignalType(int signalType);

	/// \brief Sets the target Opus encoder bitrate, in bits per second.
	/// Applied to channels opened after this call. Pass 0 to leave Opus at its own
	/// default. Callers that care about bandwidth must set this: Opus otherwise picks a
	/// rate from the sample rate alone.
	/// \param[in] bitsPerSecond target bitrate, or 0 for the Opus default
	void SetEncoderBitrate(int bitsPerSecond);

	/// \brief Returns current state of VAD (DTX).
	/// \return true if VAD is enabled, false otherwise
	bool IsVADActive(void);

	/// \brief Returns the current state of the noise filter
	/// \return true if the noise filter is active, false otherwise.
	bool IsNoiseFilterActive();

	/// \brief Returns the current state of VBR
	/// \return true if VBR is active, false otherwise.
	bool IsVBRActive();

	/// Shuts down RakVoice
	void Deinit(void);

	/// \brief Opens a channel to another connected system
	/// You will get ID_RAKVOICE_OPEN_CHANNEL_REPLY on success
	/// \param[in] recipient Which system to open a channel to
	void RequestVoiceChannel(RakNetGUID recipient);

	/// \brief Closes an existing voice channel.
	/// Other system will get ID_RAKVOICE_CLOSE_CHANNEL
	/// \param[in] recipient Which system to close a channel with
	void CloseVoiceChannel(RakNetGUID recipient);

	/// \brief Closes all existing voice channels
	/// Other systems will get ID_RAKVOICE_CLOSE_CHANNEL
	void CloseAllChannels(void);

	/// \brief Sends voice data to a system on an open channel
	/// \pre \a recipient must refer to a system with an open channel via RequestVoiceChannel
	/// \param[in] recipient The system to send voice data to
	/// \param[in] inputBuffer The voice data. The size of inputBuffer should be what was specified as bufferSizeBytes in Init
	bool SendFrame(RakNetGUID recipient, void *inputBuffer);

	/// \brief Returns if we are currently sending voice data
	/// \param[in] recipient Which system to check
	/// \return If we are sending voice data for the specified system
	bool IsSendingVoiceDataTo(RakNetGUID recipient);

	/// \brief Gets decoded voice data, from one or more remote senders
	/// \param[out] outputBuffer The voice data. The size of outputBuffer should be what was specified as bufferSizeBytes in Init
	void ReceiveFrame(void *outputBuffer);

	/// Returns the sample rate, as passed to Init
	/// \return the sample rate
	int GetSampleRate(void) const;

	/// Returns the buffer size in bytes, as passed to Init
	/// \return buffer size in bytes
	int GetBufferSizeBytes(void) const;

	/// Returns true or false, indicating if the object has been initialized
	/// \return true if initialized, false otherwise.
	bool IsInitialized(void) const;

	/// Returns the RakPeerInterface that the object is attached to.
	/// \return the respective RakPeerInterface, or nullptr if not attached.
	RakPeerInterface* GetRakPeerInterface(void) const;

	/// How many bytes are on the write buffer, waiting to be passed to a call to RakPeer::Send
	/// \param[in] guid The system to query, or UNASSIGNED_RAKNET_GUID for the sum of all channels.
	/// \return Number of bytes on the write buffer
	unsigned GetBufferedBytesToSend(RakNetGUID guid) const;

	/// How many bytes are on the read buffer, waiting to be passed to a call to ReceiveFrame
	/// \param[in] guid The system to query, or UNASSIGNED_RAKNET_GUID for the sum of all channels.
	/// \return Number of bytes on the read buffer.
	unsigned GetBufferedBytesToReturn(RakNetGUID guid) const;

	/// Enables/disables loopback mode
	/// \param[in] enabled true to enable, false to disable
	void SetLoopbackMode(bool enabled);

	/// Returns true or false, indicating if the loopback mode is enabled
	/// \return true if enabled, false otherwise.
	bool IsLoopbackMode(void) const;

	/// \brief Routes all outgoing frames through a relay server instead of directly to peers.
	/// In relay mode SendFrame() sends to the relay target and stamps our own GUID as the
	/// frame origin, and incoming frames are keyed by that origin rather than the sender.
	void SetRelayMode(bool enable);

	/// \brief Sets the server that relay-mode frames are sent to.
	void SetRelayTarget(RakNetGUID server);

	/// \brief Marks this peer as the relay host (the server).
	/// A relay host never decodes: incoming ID_RAKVOICE_RELAY_DATA packets are passed
	/// through to the application loop so they can be handed back to RelayFrame().
	/// Distinct from SetRelayMode(), which is what a talking/listening client enables.
	void SetRelayHost(bool enable);

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

	// --------------------------------------------------------------------------------------------
	// Message handling functions
	// --------------------------------------------------------------------------------------------
	virtual void OnShutdown(void);
	virtual void Update(void);
	virtual PluginReceiveResult OnReceive(Packet *packet);
	virtual void OnClosedConnection(const SystemAddress &systemAddress, RakNetGUID rakNetGUID, PI2_LostConnectionReason lostConnectionReason );

protected:
	void OnOpenChannelRequest(Packet *packet);
	void OnOpenChannelReply(Packet *packet);
	virtual void OnVoiceData(Packet *packet);
	void OpenChannel(Packet *packet);
	void FreeChannelMemory(RakNetGUID recipient);
	void FreeChannelMemory(unsigned index, bool removeIndex);
	void WriteOutputToChannel(VoiceChannel *channel, char *dataToWrite, int bytesToWrite);
	void DecodeIntoChannel(VoiceChannel *channel, unsigned short packetMessageNumber,
		const unsigned char *payload, unsigned payloadLength);
	VoiceChannel *GetOrCreateChannel(RakNetGUID origin);
	void OnRelayVoiceData(Packet *packet);

	/// Get frame size in samples for the given sample rate (20ms frames)
	static int GetFrameSizeSamples(int sampleRate);

	DataStructures::OrderedList<RakNetGUID, VoiceChannel*, VoiceChannelComp> voiceChannels;
	int32_t sampleRate;
	unsigned bufferSizeBytes;
	float *bufferedOutput;
	unsigned bufferedOutputCount;
	bool zeroBufferedOutput;
	bool defaultVADState;
	bool defaultDENOISEState;
	/// Target encoder bitrate in bits per second; 0 leaves Opus at its own default.
	int defaultBitrate;
	bool defaultVBRState;
	int defaultSignalType;
	bool loopbackMode;
	bool relayMode;
	bool relayHost;
	bool perSpeakerOutput;
	RakNetGUID relayTarget;
};

} // namespace MafiaNet

#endif
