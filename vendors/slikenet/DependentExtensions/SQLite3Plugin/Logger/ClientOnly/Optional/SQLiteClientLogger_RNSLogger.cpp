/*
 * This file was taken from RakNet 4.082.
 * Please see licenses/RakNet license.txt for the underlying license and related copyright.
 *
 * Modified work: Copyright (c) 2017, SLikeSoft UG (haftungsbeschränkt)
 *
 * This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
 * license found in the license.txt file in the root directory of this source tree.
 */

#include "SQLiteClientLogger_RNSLogger.h"
#include "slikenet/time.h"
#include "slikenet/GetTime.h"
#include "slikenet/statistics.h"
#include "slikenet/peerinterface.h"
#include "SQLiteClientLoggerPlugin.h"

using namespace SLNet;

static const char *DEFAULT_RAKNET_STATISTICS_TABLE="RakNetStatistics";

SQLiteClientLogger_RakNetStatistics::SQLiteClientLogger_RakNetStatistics()
{
	lastUpdate=0;
}
SQLiteClientLogger_RakNetStatistics::~SQLiteClientLogger_RakNetStatistics()
{
}
void SQLiteClientLogger_RakNetStatistics::Update(void)
{
	SLNet::TimeUS time = SLNet::GetTimeUS();
	if (time-lastUpdate>1000000)
	{
		lastUpdate=time;
		unsigned int i;
		RakNetStatistics rns;
		for (i=0; i < rakPeerInterface->GetMaximumNumberOfPeers(); i++)
		{
			if (rakPeerInterface->GetStatistics( i, &rns ))
			{
				/*
				rns.valueOverLastSecond[USER_MESSAGE_BYTES_PUSHED],
					rns.valueOverLastSecond[USER_MESSAGE_BYTES_SENT],
					rns.valueOverLastSecond[USER_MESSAGE_BYTES_RESENT],
					rns.valueOverLastSecond[USER_MESSAGE_BYTES_RECEIVED_PROCESSED],
					rns.valueOverLastSecond[USER_MESSAGE_BYTES_RECEIVED_IGNORED],
					rns.valueOverLastSecond[ACTUAL_BYTES_SENT],
					rns.valueOverLastSecond[ACTUAL_BYTES_RECEIVED],
					rns.runningTotal[USER_MESSAGE_BYTES_PUSHED],
					rns.runningTotal[USER_MESSAGE_BYTES_SENT],
					rns.runningTotal[USER_MESSAGE_BYTES_RESENT],
					rns.runningTotal[USER_MESSAGE_BYTES_RECEIVED_PROCESSED],
					rns.runningTotal[USER_MESSAGE_BYTES_RECEIVED_IGNORED],
					rns.runningTotal[ACTUAL_BYTES_SENT],
					rns.runningTotal[ACTUAL_BYTES_RECEIVED],
					rns.connectionStartTime,
					rns.BPSLimitByCongestionControl,
					rns.isLimitedByCongestionControl,
					rns.BPSLimitByOutgoingBandwidthLimit,
					rns.isLimitedByOutgoingBandwidthLimit,
					rns.messageInSendBuffer[IMMEDIATE_PRIORITY],
					rns.messageInSendBuffer[HIGH_PRIORITY],
					rns.messageInSendBuffer[MEDIUM_PRIORITY],
					rns.messageInSendBuffer[LOW_PRIORITY],
					rns.bytesInSendBuffer[IMMEDIATE_PRIORITY],
					rns.bytesInSendBuffer[HIGH_PRIORITY],
					rns.bytesInSendBuffer[MEDIUM_PRIORITY],
					rns.bytesInSendBuffer[LOW_PRIORITY],
					rns.messagesInResendBuffer,
					rns.bytesInResendBuffer,
					rns.packetlossLastSecond,
					rns.packetlossTotal,
					*/

			

				rakSqlLog(
					DEFAULT_RAKNET_STATISTICS_TABLE,
					"valueOverLastSecond[USER_MESSAGE_BYTES_PUSHED],"
					"valueOverLastSecond[USER_MESSAGE_BYTES_SENT],"
					"valueOverLastSecond[USER_MESSAGE_BYTES_RESENT],"
					"valueOverLastSecond[USER_MESSAGE_BYTES_RECEIVED_PROCESSED],"
					"valueOverLastSecond[USER_MESSAGE_BYTES_RECEIVED_IGNORED],"
					"valueOverLastSecond[ACTUAL_BYTES_SENT],"
					"valueOverLastSecond[ACTUAL_BYTES_RECEIVED],"
					"BPSLimitByCongestionControl,"
					"BPSLimitByOutgoingBandwidthLimit,"
					"bytesInSendBuffer,"
					"messagesInResendBuffer,"
					"bytesInResendBuffer,"
					"packetlossLastSecond,"
					"packetlossTotal",
					( \
					rns.valueOverLastSecond[USER_MESSAGE_BYTES_PUSHED], \
					rns.valueOverLastSecond[USER_MESSAGE_BYTES_SENT], \
					rns.valueOverLastSecond[USER_MESSAGE_BYTES_RESENT], \
					rns.valueOverLastSecond[USER_MESSAGE_BYTES_RECEIVED_PROCESSED], \
					rns.valueOverLastSecond[USER_MESSAGE_BYTES_RECEIVED_IGNORED], \
					rns.valueOverLastSecond[ACTUAL_BYTES_SENT], \
					rns.valueOverLastSecond[ACTUAL_BYTES_RECEIVED], \
					rns.BPSLimitByCongestionControl, \
					rns.BPSLimitByOutgoingBandwidthLimit, \
					rns.bytesInSendBuffer[IMMEDIATE_PRIORITY]+rns.bytesInSendBuffer[HIGH_PRIORITY]+rns.bytesInSendBuffer[MEDIUM_PRIORITY]+rns.bytesInSendBuffer[LOW_PRIORITY], \
					rns.messagesInResendBuffer, \
					rns.bytesInResendBuffer, \
					rns.packetlossLastSecond, \
					rns.packetlossTotal \
					));
			}
		}
	}
}
