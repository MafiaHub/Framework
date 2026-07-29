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

#include "mafianet/RakVoice.h"
#include <opus.h>
#include <rnnoise.h>
#include "mafianet/BitStream.h"
#include "mafianet/PacketPriority.h"
#include "mafianet/MessageIdentifiers.h"
#include "mafianet/peerinterface.h"
#include <stdlib.h>
#include <cstring>
#include "mafianet/GetTime.h"

#ifdef _DEBUG
#include <stdio.h>
#endif

using namespace MafiaNet;

#define SAMPLESIZE 2
// Single definition lives in RakVoice.h so relay hosts can bound inbound frames.
#define MAX_OPUS_PACKET_SIZE ((int)MafiaNet::RAKVOICE_MAX_OPUS_PACKET_SIZE)

// RNNoise frame size is fixed at 480 samples (10ms at 48kHz)
#define RNNOISE_FRAME_SIZE 480

int MafiaNet::VoiceChannelComp( const RakNetGUID &key, VoiceChannel * const &data )
{
	if (key < data->guid)
		return -1;
	if (key == data->guid)
		return 0;
	return 1;
}

RakVoice::RakVoice()
{
	bufferedOutput = nullptr;
	// Only assigned in Init(), but Update() reads them and a relay host never calls Init().
	// Leaving them indeterminate makes that read UB, and a non-zero zeroBufferedOutput would
	// send the zeroing loop through the null bufferedOutput for a garbage count.
	bufferedOutputCount = 0;
	zeroBufferedOutput = false;
	defaultVADState = true;
	defaultDENOISEState = false;
	defaultBitrate = 0;
	defaultVBRState = false;
	defaultSignalType = OPUS_SIGNAL_VOICE;
	loopbackMode = false;
	sampleRate = 0;
	bufferSizeBytes = 0;
	relayMode = false;
	relayHost = false;
	perSpeakerOutput = false;
	relayTarget = UNASSIGNED_RAKNET_GUID;
}

RakVoice::~RakVoice()
{
	Deinit();
}

int RakVoice::GetFrameSizeSamples(int sampleRate)
{
	// 20ms frame size for all sample rates
	return sampleRate / 50;  // sampleRate * 0.020
}

void RakVoice::Init(unsigned short newSampleRate, unsigned newBufferSizeBytes)
{
	RakAssert(newSampleRate == 8000 || newSampleRate == 16000 || newSampleRate == 24000 || newSampleRate == 48000);
	sampleRate = newSampleRate;
	bufferSizeBytes = newBufferSizeBytes;
	bufferedOutputCount = newBufferSizeBytes / SAMPLESIZE;
	bufferedOutput = (float*)rakMalloc_Ex(sizeof(float) * bufferedOutputCount, _FILE_AND_LINE_);
	for (unsigned i = 0; i < bufferedOutputCount; i++)
		bufferedOutput[i] = 0.0f;
	zeroBufferedOutput = false;

	if (relayMode && rakPeerInterface != nullptr)
	{
		// Local encoder state for the outgoing stream; see SetRelayMode.
		GetOrCreateChannel(rakPeerInterface->GetMyGUID());
	}
}

void RakVoice::Deinit(void)
{
	if (bufferedOutput)
	{
		rakFree_Ex(bufferedOutput, _FILE_AND_LINE_);
		bufferedOutput = nullptr;
		CloseAllChannels();
	}
}

void RakVoice::SetLoopbackMode(bool enabled)
{
	if (enabled)
	{
		Packet p;
		MafiaNet::BitStream out;
		out.Write((unsigned char)ID_RAKVOICE_OPEN_CHANNEL_REQUEST);
		out.Write((int32_t)sampleRate);
		p.data = out.GetData();
		p.systemAddress = MafiaNet::UNASSIGNED_SYSTEM_ADDRESS;
		p.guid = UNASSIGNED_RAKNET_GUID;
		p.length = out.GetNumberOfBytesUsed();
		OpenChannel(&p);
	}
	else
	{
		FreeChannelMemory(UNASSIGNED_RAKNET_GUID);
	}
	loopbackMode = enabled;
}

bool RakVoice::IsLoopbackMode(void) const
{
	return loopbackMode;
}

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

void RakVoice::SetRelayHost(bool enable)
{
	relayHost = enable;
}

void RakVoice::SetPerSpeakerOutput(bool enable)
{
	perSpeakerOutput = enable;
}

RakNetGUID RakVoice::ReadRelayOrigin(Packet *packet)
{
	// Offsets are derived in RakVoice.h, so this stays correct as the header grows.
	if (packet == nullptr || packet->length < RAKVOICE_RELAY_OFFSET_ORIGIN + sizeof(uint64_t))
		return UNASSIGNED_RAKNET_GUID;

	// Reject unknown wire formats before trusting any field position.
	if (packet->data[RAKVOICE_RELAY_OFFSET_VERSION] != RAKVOICE_RELAY_FORMAT_VERSION)
		return UNASSIGNED_RAKNET_GUID;

	RakNetGUID origin;
	memcpy(&origin.g, packet->data + RAKVOICE_RELAY_OFFSET_ORIGIN, sizeof(uint64_t));
	return origin;
}

void RakVoice::RelayFrame(Packet *packet, const RakNetGUID *recipients, int count)
{
	// Forward the payload untouched. The origin GUID is already in the header, written by
	// the talker, so the server does not need to rewrite anything and never decodes.
	if (packet == nullptr || rakPeerInterface == nullptr)
		return;

	for (int i = 0; i < count; i++)
	{
		// Unreliable, NOT UnreliableSequenced: every relayed frame reaches the recipient from
		// a single sender (the server) on ordering channel 0, so all speakers would share one
		// sequence stream and whichever speaker lost the race would be discarded whenever two
		// people talk at once. The relay header carries a per-speaker sequence number that
		// already drives PLC, so ordering is handled a layer up.
		rakPeerInterface->Send((const char*)packet->data, packet->length,
			MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable, 0, recipients[i], false);
	}
}

void RakVoice::RequestVoiceChannel(RakNetGUID recipient)
{
	MafiaNet::BitStream out;
	out.Write((unsigned char)ID_RAKVOICE_OPEN_CHANNEL_REQUEST);
	out.Write((int32_t)sampleRate);
	SendUnified(&out, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, 0, recipient, false);
}

void RakVoice::CloseVoiceChannel(RakNetGUID recipient)
{
	// Tested before FreeChannelMemory, which removes the entry. Without this a peer that
	// never opened a channel -- every peer on a server that attaches RakVoice purely as a
	// relay host -- would still be sent ID_RAKVOICE_CLOSE_CHANNEL on a clean disconnect,
	// which a client with no RakVoice attached sees as an unknown packet id.
	if (!voiceChannels.HasData(recipient))
		return;

	FreeChannelMemory(recipient);
	MafiaNet::BitStream out;
	out.Write((unsigned char)ID_RAKVOICE_CLOSE_CHANNEL);
	SendUnified(&out, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, 0, recipient, false);
}

void RakVoice::CloseAllChannels(void)
{
	MafiaNet::BitStream out;
	out.Write((unsigned char)ID_RAKVOICE_CLOSE_CHANNEL);

	for (unsigned index = 0; index < voiceChannels.Size(); index++)
	{
		SendUnified(&out, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, 0, voiceChannels[index]->guid, false);
		FreeChannelMemory(index, false);
	}

	voiceChannels.Clear(false, _FILE_AND_LINE_);
}

bool RakVoice::SendFrame(RakNetGUID recipient, void *inputBuffer)
{
	bool objectExists;
	unsigned index;
	VoiceChannel *channel;

	index = voiceChannels.GetIndexFromKey(recipient, &objectExists);
	if (objectExists)
	{
		unsigned totalBufferSize;
		unsigned remainingBufferSize;

		channel = voiceChannels[index];

		totalBufferSize = bufferSizeBytes * FRAME_OUTGOING_BUFFER_COUNT;
		if (channel->outgoingWriteIndex >= channel->outgoingReadIndex)
			remainingBufferSize = totalBufferSize - (channel->outgoingWriteIndex - channel->outgoingReadIndex);
		else
			remainingBufferSize = channel->outgoingReadIndex - channel->outgoingWriteIndex;

		RakAssert(remainingBufferSize > 0 && remainingBufferSize <= totalBufferSize);

		memcpy(channel->outgoingBuffer + channel->outgoingWriteIndex, inputBuffer, bufferSizeBytes);

		RakAssert(channel->outgoingWriteIndex + bufferSizeBytes <= totalBufferSize);

		channel->outgoingWriteIndex += bufferSizeBytes;
		RakAssert(channel->outgoingWriteIndex <= totalBufferSize);
		if (channel->outgoingWriteIndex == totalBufferSize)
			channel->outgoingWriteIndex = 0;

		if (bufferSizeBytes >= remainingBufferSize)
		{
			RakAssert(0); // Buffer overflow warning
			channel->outgoingReadIndex = (channel->outgoingReadIndex + channel->frameSizeSamples * SAMPLESIZE) % totalBufferSize;
		}

		return true;
	}

	return false;
}

bool RakVoice::IsSendingVoiceDataTo(RakNetGUID recipient)
{
	bool objectExists;
	unsigned index;
	index = voiceChannels.GetIndexFromKey(recipient, &objectExists);

	if (objectExists)
		return voiceChannels[index]->isSendingVoiceData;
	return false;
}

void RakVoice::ReceiveFrame(void *outputBuffer)
{
	short *out = (short*)outputBuffer;
	for (unsigned i = 0; i < bufferSizeBytes / SAMPLESIZE; i++)
	{
		if (bufferedOutput[i] > 32767.0f)
			out[i] = 32767;
		else if (bufferedOutput[i] < -32768.0f)
			out[i] = -32768;
		else
			out[i] = (short)bufferedOutput[i];
	}

	zeroBufferedOutput = true;
}

int RakVoice::GetSampleRate(void) const
{
	return sampleRate;
}

int RakVoice::GetBufferSizeBytes(void) const
{
	return bufferSizeBytes;
}

bool RakVoice::IsInitialized(void) const
{
	return (bufferedOutput != nullptr);
}

RakPeerInterface* RakVoice::GetRakPeerInterface(void) const
{
	return rakPeerInterface;
}

unsigned RakVoice::GetBufferedBytesToSend(RakNetGUID guid) const
{
	bool objectExists;
	VoiceChannel *channel;
	unsigned totalBufferSize = bufferSizeBytes * FRAME_OUTGOING_BUFFER_COUNT;

	if (guid != UNASSIGNED_RAKNET_GUID)
	{
		unsigned index = voiceChannels.GetIndexFromKey(guid, &objectExists);
		if (objectExists)
		{
			channel = voiceChannels[index];
			if (channel->outgoingWriteIndex >= channel->outgoingReadIndex)
				return channel->outgoingWriteIndex - channel->outgoingReadIndex;
			else
				return channel->outgoingWriteIndex + (totalBufferSize - channel->outgoingReadIndex);
		}
	}
	else
	{
		unsigned total = 0;
		for (unsigned i = 0; i < voiceChannels.Size(); i++)
		{
			channel = voiceChannels[i];
			if (channel->outgoingWriteIndex >= channel->outgoingReadIndex)
				total += channel->outgoingWriteIndex - channel->outgoingReadIndex;
			else
				total += channel->outgoingWriteIndex + (totalBufferSize - channel->outgoingReadIndex);
		}
		return total;
	}

	return 0;
}

unsigned RakVoice::GetBufferedBytesToReturn(RakNetGUID guid) const
{
	bool objectExists;
	VoiceChannel *channel;
	unsigned totalBufferSize = bufferSizeBytes * FRAME_OUTGOING_BUFFER_COUNT;

	if (guid != UNASSIGNED_RAKNET_GUID)
	{
		unsigned index = voiceChannels.GetIndexFromKey(guid, &objectExists);
		if (objectExists)
		{
			channel = voiceChannels[index];
			if (channel->incomingReadIndex <= channel->incomingWriteIndex)
				return channel->incomingWriteIndex - channel->incomingReadIndex;
			else
				return totalBufferSize - channel->incomingReadIndex + channel->incomingWriteIndex;
		}
	}
	else
	{
		unsigned total = 0;
		for (unsigned i = 0; i < voiceChannels.Size(); i++)
		{
			channel = voiceChannels[i];
			if (channel->incomingReadIndex <= channel->incomingWriteIndex)
				total += channel->incomingWriteIndex - channel->incomingReadIndex;
			else
				total += totalBufferSize - channel->incomingReadIndex + channel->incomingWriteIndex;
		}
		return total;
	}
	return 0;
}

void RakVoice::OnShutdown(void)
{
	CloseAllChannels();
}

void RakVoice::Update(void)
{
	unsigned i, j, bytesAvailable, opusFramesAvailable, opusBlockSize;
	unsigned bytesWaitingToReturn;
	VoiceChannel *channel;
	char *inputBuffer;
	unsigned char encodedBuffer[MAX_OPUS_PACKET_SIZE];
	char tempOutput[2048];
	static const int headerSize = sizeof(unsigned char) + sizeof(unsigned short);

	// A relay host attaches the plugin without ever calling Init(): it forwards payloads
	// through RelayFrame() and owns no codec. Everything below operates on the buffered
	// output and the voice channels, neither of which exists before Init(), so there is
	// genuinely nothing to do -- including for a relay host, whose channel list is always
	// empty (OnOpenChannelRequest refuses to open one while bufferedOutput is null).
	if (!IsInitialized())
		return;

	tempOutput[0] = ID_RAKVOICE_DATA;

	MafiaNet::TimeMS currentTime = MafiaNet::GetTimeMS();
	unsigned totalBufferSize = bufferSizeBytes * FRAME_OUTGOING_BUFFER_COUNT;

	if (zeroBufferedOutput)
	{
		for (i = 0; i < bufferedOutputCount; i++)
			bufferedOutput[i] = 0.0f;
		for (i = 0; i < voiceChannels.Size(); i++)
			voiceChannels[i]->copiedOutgoingBufferToBufferedOutput = false;
		zeroBufferedOutput = false;
	}

	if (relayMode)
	{
		// Relay speakers are peers of the server, not of us, so OnClosedConnection never
		// fires for them and nothing else would ever free their channels. Reap the ones we
		// have stopped hearing from. Walked backwards so removal cannot skip an entry.
		// Never reaps the self-keyed channel: it holds our outgoing encoder state, which is
		// written by SendFrame() and never decoded into.
		RakNetGUID selfGuid = rakPeerInterface != nullptr ? rakPeerInterface->GetMyGUID() : UNASSIGNED_RAKNET_GUID;
		for (i = voiceChannels.Size(); i > 0; i--)
		{
			channel = voiceChannels[i - 1];
			if (channel->guid == selfGuid)
				continue;
			if (currentTime - channel->lastDecode > RAKVOICE_RELAY_CHANNEL_TIMEOUT_MS)
				FreeChannelMemory(i - 1, true);
		}
	}

	for (i = 0; i < voiceChannels.Size(); i++)
	{
		channel = voiceChannels[i];

		if (currentTime - channel->lastSend > 50) // Throttle to 20 sends a second
		{
			channel->isSendingVoiceData = false;

			if (channel->outgoingWriteIndex >= channel->outgoingReadIndex)
				bytesAvailable = channel->outgoingWriteIndex - channel->outgoingReadIndex;
			else
				bytesAvailable = channel->outgoingWriteIndex + (totalBufferSize - channel->outgoingReadIndex);

			opusBlockSize = channel->frameSizeSamples * SAMPLESIZE;
			opusFramesAvailable = bytesAvailable / opusBlockSize;

			// A decode-only relay channel has no encoder; it also never has outgoing bytes,
			// so this is belt and braces. Always true off the relay path.
			if (opusFramesAvailable > 0 && channel->encoder != nullptr)
			{
				while (opusFramesAvailable-- > 0)
				{
					// Get input buffer, handling wrap-around
					if (channel->outgoingReadIndex + opusBlockSize > totalBufferSize)
					{
						// Copy wrapped data to temp buffer
						unsigned firstPart = totalBufferSize - channel->outgoingReadIndex;
						memcpy(tempOutput + headerSize, channel->outgoingBuffer + channel->outgoingReadIndex, firstPart);
						memcpy(tempOutput + headerSize + firstPart, channel->outgoingBuffer, opusBlockSize - firstPart);
						inputBuffer = tempOutput + headerSize;
					}
					else
					{
						inputBuffer = channel->outgoingBuffer + channel->outgoingReadIndex;
					}

					short *samples = (short*)inputBuffer;

					// Apply RNNoise denoising if enabled
					if (defaultDENOISEState && channel->denoiser)
					{
						// RNNoise works at 48kHz with 480 sample frames
						// For other sample rates, we skip denoising (or could resample)
						//
						// NOTE: unreachable at the MafiaHub Framework's frame size.
						// GetFrameSizeSamples() returns sampleRate/50, i.e. 960 (20ms) at 48kHz,
						// which is what Framework::Voice::kFrameSamples uses, so this branch never
						// runs and voice ships undenoised. Fixing it means either 10ms frames or
						// running RNNoise twice per frame with shared state; see the voice design
						// spec, deferred to M2.
						if (sampleRate == 48000 && channel->frameSizeSamples == RNNOISE_FRAME_SIZE)
						{
							float floatSamples[RNNOISE_FRAME_SIZE];
							for (int s = 0; s < RNNOISE_FRAME_SIZE; s++)
								floatSamples[s] = (float)samples[s];

							rnnoise_process_frame(channel->denoiser, floatSamples, floatSamples);

							for (int s = 0; s < RNNOISE_FRAME_SIZE; s++)
								samples[s] = (short)floatSamples[s];
						}
					}

					// Encode with Opus
					// Bound the encoder by what actually fits in tempOutput after the largest
					// header, not by MAX_OPUS_PACKET_SIZE (4000 > sizeof(tempOutput)).
					int encodedBytes = opus_encode(channel->encoder, samples, channel->frameSizeSamples,
					                               encodedBuffer, (opus_int32)(sizeof(tempOutput) - RAKVOICE_RELAY_HEADER_SIZE));

					channel->outgoingReadIndex = (channel->outgoingReadIndex + opusBlockSize) % totalBufferSize;

					if (encodedBytes < 0)
					{
						// Opus encoding error
						continue;
					}

					// DTX: if encoded bytes is very small (just a DTX packet), skip if VAD enabled
					if (defaultVADState && encodedBytes <= 2)
					{
						continue;
					}

					channel->isSendingVoiceData = true;

					if (relayMode)
					{
						// Build packet: ID (1 byte) + format version (1 byte) + origin guid (8 bytes)
						// + channel id (2 bytes) + message number (2 bytes) + encoded data
						RakNetGUID self = rakPeerInterface->GetMyGUID();
						uint16_t channelId = 0; // M1: proximity only. M2 sets this per channel.

						tempOutput[0] = ID_RAKVOICE_RELAY_DATA;
						tempOutput[RAKVOICE_RELAY_OFFSET_VERSION] = (char)RAKVOICE_RELAY_FORMAT_VERSION;
						memcpy(tempOutput + RAKVOICE_RELAY_OFFSET_ORIGIN, &self.g, sizeof(uint64_t));
						memcpy(tempOutput + RAKVOICE_RELAY_OFFSET_CHANNEL_ID, &channelId, sizeof(uint16_t));
						memcpy(tempOutput + RAKVOICE_RELAY_OFFSET_SEQUENCE, &channel->outgoingMessageNumber, sizeof(unsigned short));
						memcpy(tempOutput + RAKVOICE_RELAY_HEADER_SIZE, encodedBuffer, encodedBytes);
						channel->outgoingMessageNumber++;

						rakPeerInterface->Send(tempOutput, encodedBytes + (int)RAKVOICE_RELAY_HEADER_SIZE,
							MafiaNet::Priority::High, MafiaNet::Reliability::UnreliableSequenced, 0, relayTarget, false);
					}
					else
					{
						// Build packet: ID (1 byte) + message number (2 bytes) + encoded data
						tempOutput[0] = ID_RAKVOICE_DATA;
						memcpy(tempOutput + 1, &channel->outgoingMessageNumber, sizeof(unsigned short));
						memcpy(tempOutput + headerSize, encodedBuffer, encodedBytes);
						channel->outgoingMessageNumber++;

						MafiaNet::BitStream tempOutputBs((unsigned char*)tempOutput, encodedBytes + headerSize, false);
						SendUnified(&tempOutputBs, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable, 0, channel->guid, false);

						if (loopbackMode)
						{
							Packet p;
							p.length = encodedBytes + headerSize;
							p.data = (unsigned char*)tempOutput;
							p.guid = channel->guid;
							p.systemAddress = rakPeerInterface->GetSystemAddressFromGuid(p.guid);
							OnVoiceData(&p);
						}
					}
				}

				channel->lastSend = currentTime;
			}
		}

		// Mix incoming audio to output buffer.
		// With per-speaker output the ring buffers still fill, but nothing is summed here:
		// ReceiveFrameFrom() drains each speaker individually instead.
		if (perSpeakerOutput == false && channel->copiedOutgoingBufferToBufferedOutput == false)
		{
			if (channel->incomingReadIndex <= channel->incomingWriteIndex)
				bytesWaitingToReturn = channel->incomingWriteIndex - channel->incomingReadIndex;
			else
				bytesWaitingToReturn = totalBufferSize - channel->incomingReadIndex + channel->incomingWriteIndex;

			if (bytesWaitingToReturn == 0)
			{
				channel->bufferOutput = true;
			}
			else if (channel->bufferOutput == false || bytesWaitingToReturn > bufferSizeBytes * 2)
			{
				channel->copiedOutgoingBufferToBufferedOutput = true;
				channel->bufferOutput = false;

				if (bytesWaitingToReturn > bufferSizeBytes)
				{
					bytesWaitingToReturn = bufferSizeBytes;
				}
				else
				{
					channel->incomingWriteIndex = channel->incomingReadIndex + bufferSizeBytes;
					if (channel->incomingWriteIndex >= totalBufferSize)
						channel->incomingWriteIndex -= totalBufferSize;
				}

				short *in = (short*)(channel->incomingBuffer + channel->incomingReadIndex);
				for (j = 0; j < bytesWaitingToReturn / SAMPLESIZE; j++)
				{
					bufferedOutput[j] += in[j % (totalBufferSize / SAMPLESIZE)];
				}

				channel->incomingReadIndex += bufferSizeBytes;
				if (channel->incomingReadIndex >= totalBufferSize)
					channel->incomingReadIndex -= totalBufferSize;
			}
		}
	}
}

PluginReceiveResult RakVoice::OnReceive(Packet *packet)
{
	RakAssert(packet);

	switch (packet->data[0])
	{
	case ID_RAKVOICE_OPEN_CHANNEL_REQUEST:
		OnOpenChannelRequest(packet);
		break;
	case ID_RAKVOICE_OPEN_CHANNEL_REPLY:
		OnOpenChannelReply(packet);
		break;
	case ID_RAKVOICE_CLOSE_CHANNEL:
		FreeChannelMemory(packet->guid);
		break;
	case ID_RAKVOICE_DATA:
		OnVoiceData(packet);
		return RR_STOP_PROCESSING_AND_DEALLOCATE;
	case ID_RAKVOICE_RELAY_DATA:
		// A relay host never decodes. Plugin OnReceive runs inside RakPeer::Receive, before
		// the packet reaches the application loop, so swallowing it here would leave the
		// server with nothing to hand to RelayFrame(). Pass it through instead.
		if (relayHost)
			return RR_CONTINUE_PROCESSING;
		OnRelayVoiceData(packet);
		return RR_STOP_PROCESSING_AND_DEALLOCATE;
	}

	return RR_CONTINUE_PROCESSING;
}

void RakVoice::OnClosedConnection(const SystemAddress &systemAddress, RakNetGUID rakNetGUID, PI2_LostConnectionReason lostConnectionReason)
{
	(void)systemAddress;

	if (lostConnectionReason == LCR_CLOSED_BY_USER)
		CloseVoiceChannel(rakNetGUID);
	else
		FreeChannelMemory(rakNetGUID);
}

void RakVoice::OnOpenChannelRequest(Packet *packet)
{
	if (voiceChannels.HasData(packet->guid))
		return;

	if (bufferedOutput == nullptr)
		return;

	OpenChannel(packet);

	MafiaNet::BitStream out;
	out.Write((unsigned char)ID_RAKVOICE_OPEN_CHANNEL_REPLY);
	out.Write((int32_t)sampleRate);
	SendUnified(&out, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, 0, packet->systemAddress, false);
}

void RakVoice::OnOpenChannelReply(Packet *packet)
{
	if (voiceChannels.HasData(packet->guid))
		return;
	OpenChannel(packet);
}

void RakVoice::OpenChannel(Packet *packet)
{
	MafiaNet::BitStream in(packet->data, packet->length, false);
	in.IgnoreBits(8);

	FreeChannelMemory(packet->guid);

	VoiceChannel *channel = MafiaNet::OP_NEW<VoiceChannel>(_FILE_AND_LINE_);
	channel->guid = packet->guid;
	channel->isSendingVoiceData = false;

	int newSampleRate;
	in.Read(newSampleRate);
	channel->remoteSampleRate = newSampleRate;

	if (newSampleRate != 8000 && newSampleRate != 16000 && newSampleRate != 24000 && newSampleRate != 48000)
	{
		RakAssert(0);
		MafiaNet::OP_DELETE(channel, _FILE_AND_LINE_);
		return;
	}

	// In relay mode every channel except the self-keyed one is a remote talker we only ever
	// decode, so it needs no encoder and no denoiser. Always false off the relay path, which
	// keeps the non-relay lifecycle unchanged.
	const bool decodeOnly = relayMode && rakPeerInterface != nullptr && packet->guid != rakPeerInterface->GetMyGUID();

	// Create Opus encoder
	int error = OPUS_OK;
	channel->encoder = nullptr;
	if (decodeOnly == false)
	{
		channel->encoder = opus_encoder_create(sampleRate, 1, OPUS_APPLICATION_VOIP, &error);
		if (error != OPUS_OK || channel->encoder == nullptr)
		{
			RakAssert(0);
			MafiaNet::OP_DELETE(channel, _FILE_AND_LINE_);
			return;
		}
	}

	// Create Opus decoder (using remote sample rate)
	channel->decoder = opus_decoder_create(channel->remoteSampleRate, 1, &error);
	if (error != OPUS_OK || channel->decoder == nullptr)
	{
		if (channel->encoder)
			opus_encoder_destroy(channel->encoder);
		RakAssert(0);
		MafiaNet::OP_DELETE(channel, _FILE_AND_LINE_);
		return;
	}

	// Configure encoder
	if (channel->encoder)
	{
		opus_encoder_ctl(channel->encoder, OPUS_SET_VBR(defaultVBRState ? 1 : 0));
		opus_encoder_ctl(channel->encoder, OPUS_SET_DTX(defaultVADState ? 1 : 0));
		opus_encoder_ctl(channel->encoder, OPUS_SET_SIGNAL(defaultSignalType));
		if (defaultBitrate > 0)
			opus_encoder_ctl(channel->encoder, OPUS_SET_BITRATE(defaultBitrate));
	}

	// Create RNNoise denoiser (only works well at 48kHz)
	channel->denoiser = decodeOnly ? nullptr : rnnoise_create(nullptr);

	// Calculate frame sizes
	channel->frameSizeSamples = GetFrameSizeSamples(sampleRate);
	channel->maxPacketBytes = MAX_OPUS_PACKET_SIZE;

	// Allocate buffers
	channel->outgoingBuffer = (char*)rakMalloc_Ex(bufferSizeBytes * FRAME_OUTGOING_BUFFER_COUNT, _FILE_AND_LINE_);
	channel->outgoingReadIndex = 0;
	channel->outgoingWriteIndex = 0;
	channel->bufferOutput = true;
	channel->outgoingMessageNumber = 0;
	channel->copiedOutgoingBufferToBufferedOutput = false;

	channel->incomingBuffer = (char*)rakMalloc_Ex(bufferSizeBytes * FRAME_INCOMING_BUFFER_COUNT, _FILE_AND_LINE_);
	channel->incomingReadIndex = 0;
	channel->incomingWriteIndex = 0;
	channel->lastSend = 0;
	// Stamped now, not 0: a channel that has never decoded must not be reaped on the very
	// next Update() call.
	channel->lastDecode = MafiaNet::GetTimeMS();
	channel->incomingMessageNumber = 0;

	voiceChannels.Insert(packet->guid, channel, true, _FILE_AND_LINE_);
}

void RakVoice::SetVAD(bool enable)
{
	defaultVADState = enable;
	for (unsigned int index = 0; index < voiceChannels.Size(); index++)
	{
		// Decode-only relay channels have no encoder.
		if (voiceChannels[index]->encoder == nullptr)
			continue;
		opus_encoder_ctl(voiceChannels[index]->encoder, OPUS_SET_DTX(enable ? 1 : 0));
	}
}

void RakVoice::SetNoiseFilter(bool enable)
{
	defaultDENOISEState = enable;
}

void RakVoice::SetEncoderBitrate(int bitsPerSecond)
{
	defaultBitrate = bitsPerSecond;
	for (unsigned int index = 0; index < voiceChannels.Size(); index++)
	{
		// Decode-only relay channels have no encoder.
		if (voiceChannels[index]->encoder == nullptr)
			continue;
		if (bitsPerSecond > 0)
			opus_encoder_ctl(voiceChannels[index]->encoder, OPUS_SET_BITRATE(bitsPerSecond));
	}
}

void RakVoice::SetVBR(bool enable)
{
	defaultVBRState = enable;
	for (unsigned int index = 0; index < voiceChannels.Size(); index++)
	{
		// Decode-only relay channels have no encoder.
		if (voiceChannels[index]->encoder == nullptr)
			continue;
		opus_encoder_ctl(voiceChannels[index]->encoder, OPUS_SET_VBR(enable ? 1 : 0));
	}
}

void RakVoice::SetSignalType(int signalType)
{
	defaultSignalType = signalType;
	for (unsigned int index = 0; index < voiceChannels.Size(); index++)
	{
		// Decode-only relay channels have no encoder.
		if (voiceChannels[index]->encoder == nullptr)
			continue;
		opus_encoder_ctl(voiceChannels[index]->encoder, OPUS_SET_SIGNAL(signalType));
	}
}

bool RakVoice::IsVADActive(void)
{
	return defaultVADState;
}

bool RakVoice::IsNoiseFilterActive()
{
	return defaultDENOISEState;
}

bool RakVoice::IsVBRActive()
{
	return defaultVBRState;
}

void RakVoice::FreeChannelMemory(RakNetGUID recipient)
{
	bool objectExists;
	unsigned index;
	index = voiceChannels.GetIndexFromKey(recipient, &objectExists);

	if (objectExists)
	{
		FreeChannelMemory(index, true);
	}
}

void RakVoice::FreeChannelMemory(unsigned index, bool removeIndex)
{
	VoiceChannel *channel;
	channel = voiceChannels[index];

	if (channel->encoder)
		opus_encoder_destroy(channel->encoder);
	if (channel->decoder)
		opus_decoder_destroy(channel->decoder);
	if (channel->denoiser)
		rnnoise_destroy(channel->denoiser);

	rakFree_Ex(channel->incomingBuffer, _FILE_AND_LINE_);
	rakFree_Ex(channel->outgoingBuffer, _FILE_AND_LINE_);
	MafiaNet::OP_DELETE(channel, _FILE_AND_LINE_);

	if (removeIndex)
		voiceChannels.RemoveAtIndex(index);
}

void RakVoice::OnVoiceData(Packet *packet)
{
	bool objectExists;
	unsigned index;
	unsigned short packetMessageNumber;
	static const int headerSize = sizeof(unsigned char) + sizeof(unsigned short);

	index = voiceChannels.GetIndexFromKey(packet->guid, &objectExists);
	if (objectExists)
	{
		memcpy(&packetMessageNumber, packet->data + 1, sizeof(unsigned short));

		DecodeIntoChannel(voiceChannels[index], packetMessageNumber,
			packet->data + headerSize, packet->length - headerSize);
	}
}

void RakVoice::DecodeIntoChannel(VoiceChannel *channel, unsigned short packetMessageNumber,
	const unsigned char *payload, unsigned payloadLength)
{
	unsigned short messagesSkipped;
	short decodedBuffer[960 * 2]; // Max frame size for 48kHz

	// Liveness stamp for the relay-mode reap in Update(). Unused on the non-relay path.
	channel->lastDecode = MafiaNet::GetTimeMS();

	// Intentional overflow for sequence handling
	messagesSkipped = packetMessageNumber - channel->incomingMessageNumber;
	if (messagesSkipped > ((unsigned short)-1) / 2)
	{
		// Underflow, ignore
		return;
	}

	// Handle missing packets with PLC (Packet Loss Concealment)
	int maxSkip = (int)(100.0f / (1000.0f / 50.0f)); // Max 100ms of missing audio
	int decodedFrameSize = GetFrameSizeSamples(channel->remoteSampleRate);

	for (unsigned i = 0; i < (unsigned)messagesSkipped && i < (unsigned)maxSkip; i++)
	{
		// Use Opus PLC by passing NULL for the packet
		int samples = opus_decode(channel->decoder, nullptr, 0, decodedBuffer, decodedFrameSize, 0);
		if (samples > 0)
		{
			WriteOutputToChannel(channel, (char*)decodedBuffer, samples * SAMPLESIZE);
		}
	}

	channel->incomingMessageNumber = packetMessageNumber + 1;

	// Decode the actual packet
	int samples = opus_decode(channel->decoder,
	                          payload,
	                          payloadLength,
	                          decodedBuffer,
	                          decodedFrameSize,
	                          0);

	if (samples > 0)
	{
		WriteOutputToChannel(channel, (char*)decodedBuffer, samples * SAMPLESIZE);
	}
}

VoiceChannel *RakVoice::GetOrCreateChannel(RakNetGUID origin)
{
	bool objectExists;
	unsigned index = voiceChannels.GetIndexFromKey(origin, &objectExists);
	if (objectExists)
		return voiceChannels[index];

	if (IsInitialized() == false)
		return nullptr;

	// Lazily allocate a decoder the first time we hear from this speaker. Relay speakers are
	// peers of the server rather than of us, so OnClosedConnection never fires for them;
	// they are freed only by the RAKVOICE_RELAY_CHANNEL_TIMEOUT_MS reap in Update().
	// OpenChannel() parses the remote sample rate out of the packet body, so synthesise the
	// same layout RequestVoiceChannel() would have produced, using our own sample rate.
	Packet synthetic;
	MafiaNet::BitStream out;
	out.Write((unsigned char)ID_RAKVOICE_OPEN_CHANNEL_REQUEST);
	out.Write((int32_t)sampleRate);
	synthetic.data = out.GetData();
	synthetic.length = out.GetNumberOfBytesUsed();
	synthetic.systemAddress = MafiaNet::UNASSIGNED_SYSTEM_ADDRESS;
	synthetic.guid = origin;
	OpenChannel(&synthetic);

	index = voiceChannels.GetIndexFromKey(origin, &objectExists);
	return objectExists ? voiceChannels[index] : nullptr;
}

void RakVoice::OnRelayVoiceData(Packet *packet)
{
	if (packet->length <= RAKVOICE_RELAY_HEADER_SIZE)
		return;

	// Unknown wire format: drop silently and early, before reading any other field.
	if (packet->data[RAKVOICE_RELAY_OFFSET_VERSION] != RAKVOICE_RELAY_FORMAT_VERSION)
		return;

	RakNetGUID origin = ReadRelayOrigin(packet);
	if (origin == UNASSIGNED_RAKNET_GUID || origin == rakPeerInterface->GetMyGUID())
		return;

	VoiceChannel *channel = GetOrCreateChannel(origin);
	if (channel == nullptr)
		return;

	// Reuse the existing decode path by handing it the payload in the same shape the
	// non-relay decoder gets, keyed on the channel we just resolved.
	unsigned short packetMessageNumber;
	memcpy(&packetMessageNumber, packet->data + RAKVOICE_RELAY_OFFSET_SEQUENCE, sizeof(unsigned short));

	DecodeIntoChannel(channel, packetMessageNumber,
		packet->data + RAKVOICE_RELAY_HEADER_SIZE, packet->length - RAKVOICE_RELAY_HEADER_SIZE);
}

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
	// The self-keyed channel holds outgoing encoder state in relay mode, not a remote
	// talker, so it is never reported as a speaker.
	RakNetGUID self = rakPeerInterface != nullptr ? rakPeerInterface->GetMyGUID() : UNASSIGNED_RAKNET_GUID;

	out.Clear(false, _FILE_AND_LINE_);
	for (unsigned i = 0; i < voiceChannels.Size(); i++)
	{
		if (voiceChannels[i]->guid == self)
			continue;
		out.Push(voiceChannels[i]->guid, _FILE_AND_LINE_);
	}
}

void RakVoice::WriteOutputToChannel(VoiceChannel *channel, char *dataToWrite, int bytesToWrite)
{
	unsigned totalBufferSize;
	unsigned remainingBufferSize;

	totalBufferSize = bufferSizeBytes * FRAME_INCOMING_BUFFER_COUNT;
	if (channel->incomingWriteIndex >= channel->incomingReadIndex)
		remainingBufferSize = totalBufferSize - (channel->incomingWriteIndex - channel->incomingReadIndex);
	else
		remainingBufferSize = channel->incomingReadIndex - channel->incomingWriteIndex;

	if (channel->incomingWriteIndex + bytesToWrite <= totalBufferSize)
	{
		memcpy(channel->incomingBuffer + channel->incomingWriteIndex, dataToWrite, bytesToWrite);
	}
	else
	{
		unsigned firstPart = totalBufferSize - channel->incomingWriteIndex;
		memcpy(channel->incomingBuffer + channel->incomingWriteIndex, dataToWrite, firstPart);
		memcpy(channel->incomingBuffer, dataToWrite + firstPart, bytesToWrite - firstPart);
	}
	channel->incomingWriteIndex = (channel->incomingWriteIndex + bytesToWrite) % totalBufferSize;

	if ((unsigned)bytesToWrite >= remainingBufferSize)
	{
		RakAssert(0); // Buffer overflow warning
		channel->incomingReadIndex += bufferSizeBytes;
		if (channel->incomingReadIndex >= totalBufferSize)
			channel->incomingReadIndex -= totalBufferSize;
	}
}
