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
	bool defaultVBRState;
	int defaultSignalType;
	bool loopbackMode;
};

} // namespace MafiaNet

#endif
