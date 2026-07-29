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
#define MAX_OPUS_PACKET_SIZE 4000

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
	defaultVADState = true;
	defaultDENOISEState = false;
	defaultVBRState = false;
	defaultSignalType = OPUS_SIGNAL_VOICE;
	loopbackMode = false;
	sampleRate = 0;
	bufferSizeBytes = 0;
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

void RakVoice::RequestVoiceChannel(RakNetGUID recipient)
{
	MafiaNet::BitStream out;
	out.Write((unsigned char)ID_RAKVOICE_OPEN_CHANNEL_REQUEST);
	out.Write((int32_t)sampleRate);
	SendUnified(&out, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, 0, recipient, false);
}

void RakVoice::CloseVoiceChannel(RakNetGUID recipient)
{
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

			if (opusFramesAvailable > 0)
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
					int encodedBytes = opus_encode(channel->encoder, samples, channel->frameSizeSamples,
					                               encodedBuffer, MAX_OPUS_PACKET_SIZE);

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

					// Build packet: ID (1 byte) + message number (2 bytes) + encoded data
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

				channel->lastSend = currentTime;
			}
		}

		// Mix incoming audio to output buffer
		if (channel->copiedOutgoingBufferToBufferedOutput == false)
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

	// Create Opus encoder
	int error;
	channel->encoder = opus_encoder_create(sampleRate, 1, OPUS_APPLICATION_VOIP, &error);
	if (error != OPUS_OK || channel->encoder == nullptr)
	{
		RakAssert(0);
		MafiaNet::OP_DELETE(channel, _FILE_AND_LINE_);
		return;
	}

	// Create Opus decoder (using remote sample rate)
	channel->decoder = opus_decoder_create(channel->remoteSampleRate, 1, &error);
	if (error != OPUS_OK || channel->decoder == nullptr)
	{
		opus_encoder_destroy(channel->encoder);
		RakAssert(0);
		MafiaNet::OP_DELETE(channel, _FILE_AND_LINE_);
		return;
	}

	// Configure encoder
	opus_encoder_ctl(channel->encoder, OPUS_SET_VBR(defaultVBRState ? 1 : 0));
	opus_encoder_ctl(channel->encoder, OPUS_SET_DTX(defaultVADState ? 1 : 0));
	opus_encoder_ctl(channel->encoder, OPUS_SET_SIGNAL(defaultSignalType));

	// Create RNNoise denoiser (only works well at 48kHz)
	channel->denoiser = rnnoise_create(nullptr);

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
	channel->incomingMessageNumber = 0;

	voiceChannels.Insert(packet->guid, channel, true, _FILE_AND_LINE_);
}

void RakVoice::SetVAD(bool enable)
{
	defaultVADState = enable;
	for (unsigned int index = 0; index < voiceChannels.Size(); index++)
	{
		opus_encoder_ctl(voiceChannels[index]->encoder, OPUS_SET_DTX(enable ? 1 : 0));
	}
}

void RakVoice::SetNoiseFilter(bool enable)
{
	defaultDENOISEState = enable;
}

void RakVoice::SetVBR(bool enable)
{
	defaultVBRState = enable;
	for (unsigned int index = 0; index < voiceChannels.Size(); index++)
	{
		opus_encoder_ctl(voiceChannels[index]->encoder, OPUS_SET_VBR(enable ? 1 : 0));
	}
}

void RakVoice::SetSignalType(int signalType)
{
	defaultSignalType = signalType;
	for (unsigned int index = 0; index < voiceChannels.Size(); index++)
	{
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
	unsigned short packetMessageNumber, messagesSkipped;
	VoiceChannel *channel;
	short decodedBuffer[960 * 2]; // Max frame size for 48kHz
	static const int headerSize = sizeof(unsigned char) + sizeof(unsigned short);

	index = voiceChannels.GetIndexFromKey(packet->guid, &objectExists);
	if (objectExists)
	{
		channel = voiceChannels[index];
		memcpy(&packetMessageNumber, packet->data + 1, sizeof(unsigned short));

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
		                          packet->data + headerSize,
		                          packet->length - headerSize,
		                          decodedBuffer,
		                          decodedFrameSize,
		                          0);

		if (samples > 0)
		{
			WriteOutputToChannel(channel, (char*)decodedBuffer, samples * SAMPLESIZE);
		}
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
