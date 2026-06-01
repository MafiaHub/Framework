/*
 *  Original work: Copyright (c) 2014, Oculus VR, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  RakNet License.txt file in the licenses directory of this source tree. An additional grant 
 *  of patent rights can be found in the RakNet Patents.txt file in the same directory.
 *
 *
 *  Modified work: Copyright (c) 2016-2017, SLikeSoft UG (haftungsbeschränkt)
 *
 *  This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
 *  license found in the license.txt file in the root directory of this source tree.
 */

#include "mafianet/NativeFeatureIncludes.h"
#if _RAKNET_SUPPORT_PacketLogger==1

#include "mafianet/PacketFileLogger.h"
#include "mafianet/GetTime.h"
#include "mafianet/linux_adapter.h"
#include "mafianet/osx_adapter.h"
#include "mafianet/memoryoverride.h"

using namespace MafiaNet;

STATIC_FACTORY_DEFINITIONS(PacketFileLogger,PacketFileLogger);

PacketFileLogger::PacketFileLogger()
{
	packetLogFile=0;
}
PacketFileLogger::~PacketFileLogger()
{
	if (packetLogFile)
	{
		fflush(packetLogFile);
		fclose(packetLogFile);
	}
}
void PacketFileLogger::StartLog(const char *filenamePrefix)
{
	// Open file for writing
	char filename[256];
	if (filenamePrefix)
		sprintf_s(filename, "%s_%i.csv", filenamePrefix, (int)MafiaNet::GetTimeMS());
	else
		sprintf_s(filename, "PacketLog_%i.csv", (int)MafiaNet::GetTimeMS());
	errno_t error = fopen_s(&packetLogFile, filename, "wt");
	LogHeader();
	if (error == 0)
	{
		fflush(packetLogFile);
	}
}

void PacketFileLogger::WriteLog(const char *str)
{
	if (packetLogFile)
	{
		fprintf(packetLogFile, "%s\n", str);
		fflush(packetLogFile);
	}
}

#endif // _RAKNET_SUPPORT_*
