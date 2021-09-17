/*
 * This file was taken from RakNet 4.082.
 * Please see licenses/RakNet license.txt for the underlying license and related copyright.
 *
 * Modified work: Copyright (c) 2016-2018, SLikeSoft UG (haftungsbeschränkt)
 *
 * This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
 * license found in the license.txt file in the root directory of this source tree.
 */

#include "SQLiteClientLogger_PacketLogger.h"
#include "SQLiteClientLoggerPlugin.h"
#include "slikenet/peerinterface.h"
#include "slikenet/InternalPacket.h"
#include "slikenet/MessageIdentifiers.h"

using namespace SLNet;

static const char *DEFAULT_PACKET_LOGGER_TABLE="PacketLogger";

SQLiteClientLogger_PacketLogger::SQLiteClientLogger_PacketLogger()
{
}
SQLiteClientLogger_PacketLogger::~SQLiteClientLogger_PacketLogger()
{
}
void SQLiteClientLogger_PacketLogger::OnDirectSocketSend(const char *data, const BitSize_t bitsUsed, SystemAddress remoteSystemAddress)
{
	char str1[64], str2[62], str3[64], str4[64];
	SystemAddress localSystemAddress = rakPeerInterface->GetExternalID(remoteSystemAddress);
	localSystemAddress.ToString(true, str1, static_cast<size_t>(64));
	rakPeerInterface->GetGuidFromSystemAddress(SLNet::UNASSIGNED_SYSTEM_ADDRESS).ToString(str2, 62);
	remoteSystemAddress.ToString(true, str3, static_cast<size_t>(64));
	rakPeerInterface->GetGuidFromSystemAddress(remoteSystemAddress).ToString(str4, 64);

	
	rakSqlLog(DEFAULT_PACKET_LOGGER_TABLE, "SndRcv,Type,PacketNumber,FrameNumber,PacketID,BitLength,LocalIP,LocalGuid,RemoteIP,RemoteGuid,splitPacketId,SplitPacketIndex,splitPacketCount,orderingIndex,misc", \
	          ("Snd", "Raw",0,          0,          IDTOString(data[0]), bitsUsed, str1, str2, str3, str4, "","","","","") );
}
void SQLiteClientLogger_PacketLogger::OnDirectSocketReceive(const char *data, const BitSize_t bitsUsed, SystemAddress remoteSystemAddress)
{
	char str1[64], str2[62], str3[64], str4[64];
	SystemAddress localSystemAddress = rakPeerInterface->GetExternalID(remoteSystemAddress);
	localSystemAddress.ToString(true, str1, static_cast<size_t>(64));
	rakPeerInterface->GetGuidFromSystemAddress(SLNet::UNASSIGNED_SYSTEM_ADDRESS).ToString(str2, 62);
	remoteSystemAddress.ToString(true, str3, static_cast<size_t>(64));
	rakPeerInterface->GetGuidFromSystemAddress(remoteSystemAddress).ToString(str4, 64);
	
	rakSqlLog(DEFAULT_PACKET_LOGGER_TABLE, "SndRcv,Type,PacketNumber,FrameNumber,PacketID,BitLength,LocalIP,LocalGuid,RemoteIP,RemoteGuid,splitPacketId,SplitPacketIndex,splitPacketCount,orderingIndex,misc", \
		     ("Rcv", "Raw", "",         "",         IDTOString(data[0]),bitsUsed, str1, str2, str3, str4, "","","","","") );
}
void SQLiteClientLogger_PacketLogger::OnInternalPacket(InternalPacket *internalPacket, unsigned frameNumber, SystemAddress remoteSystemAddress, SLNet::TimeMS time, bool isSend)
{
	char str1[64], str2[62], str3[64], str4[64];
	SystemAddress localSystemAddress = rakPeerInterface->GetExternalID(remoteSystemAddress);
	localSystemAddress.ToString(true, str1, static_cast<size_t>(64));
	rakPeerInterface->GetGuidFromSystemAddress(SLNet::UNASSIGNED_SYSTEM_ADDRESS).ToString(str2, 62);
	remoteSystemAddress.ToString(true, str3, static_cast<size_t>(64));
	rakPeerInterface->GetGuidFromSystemAddress(remoteSystemAddress).ToString(str4, 64);
	
	unsigned char typeByte;
	char *typeStr;
	if (internalPacket->data[0]==ID_TIMESTAMP && BITS_TO_BYTES(internalPacket->dataBitLength)>sizeof(SLNet::TimeMS)+1)
	{
		typeByte=internalPacket->data[1+sizeof(SLNet::TimeMS)];
		typeStr="Timestamp";
	}
	else
	{
		typeByte=internalPacket->data[0];
		typeStr="Normal";
	}
	
	const char* sendType = (isSend) ? "Snd" : "Rcv";
	
	rakSqlLog(DEFAULT_PACKET_LOGGER_TABLE, "SndRcv,Type,PacketNumber,FrameNumber,PacketID,BitLength,LocalIP,LocalGuid,RemoteIP,RemoteGuid,splitPacketId,SplitPacketIndex,splitPacketCount,orderingIndex,misc", \
		     (sendType, typeStr, internalPacket->reliableMessageNumber, frameNumber, IDTOString(typeByte), internalPacket->dataBitLength, str1, str2, str3, str4, internalPacket->splitPacketId, internalPacket->splitPacketIndex, internalPacket->splitPacketCount, internalPacket->orderingIndex,"") );
}
void SQLiteClientLogger_PacketLogger::OnAck(unsigned int messageNumber, SystemAddress remoteSystemAddress, SLNet::TimeMS time)
{
	char str1[64], str2[62], str3[64], str4[64];
	SystemAddress localSystemAddress = rakPeerInterface->GetExternalID(remoteSystemAddress);
	localSystemAddress.ToString(true, str1, static_cast<size_t>(64));
	rakPeerInterface->GetGuidFromSystemAddress(SLNet::UNASSIGNED_SYSTEM_ADDRESS).ToString(str2, 62);
	remoteSystemAddress.ToString(true, str3, static_cast<size_t>(64));
	rakPeerInterface->GetGuidFromSystemAddress(remoteSystemAddress).ToString(str4, 64);
	
	rakSqlLog(DEFAULT_PACKET_LOGGER_TABLE, "SndRcv,Type,PacketNumber,FrameNumber,PacketID,BitLength,LocalIP,LocalGuid,RemoteIP,RemoteGuid,splitPacketId,SplitPacketIndex,splitPacketCount,orderingIndex,misc", \
		          ("Rcv", "Ack",messageNumber, "", "", "", str1, str2, str3, str4, "","","","","") );
}
void SQLiteClientLogger_PacketLogger::OnPushBackPacket(const char *data, const BitSize_t bitsUsed, SystemAddress remoteSystemAddress)
{
	char str1[64], str2[62], str3[64], str4[64];
	SystemAddress localSystemAddress = rakPeerInterface->GetExternalID(remoteSystemAddress);
	localSystemAddress.ToString(true, str1, static_cast<size_t>(64));
	rakPeerInterface->GetGuidFromSystemAddress(SLNet::UNASSIGNED_SYSTEM_ADDRESS).ToString(str2, 62);
	remoteSystemAddress.ToString(true, str3, static_cast<size_t>(64));
	rakPeerInterface->GetGuidFromSystemAddress(remoteSystemAddress).ToString(str4, 64);
	
	rakSqlLog(DEFAULT_PACKET_LOGGER_TABLE, "SndRcv,Type,PacketNumber,FrameNumber,PacketID,BitLength,LocalIP,LocalGuid,RemoteIP,RemoteGuid,splitPacketId,SplitPacketIndex,splitPacketCount,orderingIndex,misc", \
		          ("Local", "PushBackPacket","",  "", IDTOString(data[0]), bitsUsed, str1, str2, str3, str4, "","","","","") );
}
void SQLiteClientLogger_PacketLogger::WriteMiscellaneous(const char *type, const char *msg)
{
	rakSqlLog(DEFAULT_PACKET_LOGGER_TABLE, "SndRcv,Type,PacketNumber,FrameNumber,PacketID,BitLength,LocalIP,LocalGuid,RemoteIP,RemoteGuid,splitPacketId,SplitPacketIndex,splitPacketCount,orderingIndex,misc", \
		          ("Local", type,"",  "", "", "", "", "", "","","","","","",msg) );
}
