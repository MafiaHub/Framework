/*
 *  Original work: Copyright (c) 2014, Oculus VR, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  RakNet License.txt file in the licenses directory of this source tree. An additional grant 
 *  of patent rights can be found in the RakNet Patents.txt file in the same directory.
 *
 *
 *  Modified work: Copyright (c) 2017, SLikeSoft UG (haftungsbeschränkt)
 *
 *  This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
 *  license found in the license.txt file in the root directory of this source tree.
 */

/// \file
/// \brief This will write all incoming and outgoing network messages the SQLiteClientLoggerPlugin
///



#ifndef __SQL_LITE_CLIENT_LOGGER_PACKET_LOGGER_H_
#define __SQL_LITE_CLIENT_LOGGER_PACKET_LOGGER_H_

#include "slikenet/PacketLogger.h"

namespace SLNet
{

/// \ingroup PACKETLOGGER_GROUP
/// \brief Packetlogger that outputs to a file
class RAK_DLL_EXPORT  SQLiteClientLogger_PacketLogger : public PacketLogger
{
public:
	SQLiteClientLogger_PacketLogger();
	virtual ~SQLiteClientLogger_PacketLogger();
	
	virtual void OnDirectSocketSend(const char *data, const BitSize_t bitsUsed, SystemAddress remoteSystemAddress);
	virtual void OnDirectSocketReceive(const char *data, const BitSize_t bitsUsed, SystemAddress remoteSystemAddress);
	virtual void OnInternalPacket(InternalPacket *internalPacket, unsigned frameNumber, SystemAddress remoteSystemAddress, SLNet::TimeMS time, bool isSend);
	virtual void OnAck(unsigned int messageNumber, SystemAddress remoteSystemAddress, SLNet::TimeMS time);
	virtual void OnPushBackPacket(const char *data, const BitSize_t bitsUsed, SystemAddress remoteSystemAddress);
	virtual void WriteMiscellaneous(const char *type, const char *msg);
protected:

	virtual void WriteLog(const char *str) {}
};

}

#endif
