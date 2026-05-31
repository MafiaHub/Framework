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

#include "mafianet/NativeFeatureIncludes.h"
#if _RAKNET_SUPPORT_LogCommandParser==1 && _RAKNET_SUPPORT_PacketLogger==1
#include "mafianet/PacketConsoleLogger.h"
#include "mafianet/LogCommandParser.h"
#include <stdio.h>

using namespace MafiaNet;

PacketConsoleLogger::PacketConsoleLogger()
{
	logCommandParser=0;
}

void PacketConsoleLogger::SetLogCommandParser(LogCommandParser *lcp)
{
	logCommandParser=lcp;
	if (logCommandParser)
		logCommandParser->AddChannel("PacketConsoleLogger");
}
void PacketConsoleLogger::WriteLog(const char *str)
{
	if (logCommandParser)
		logCommandParser->WriteLog("PacketConsoleLogger", str);
}

#endif // _RAKNET_SUPPORT_*
