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

#ifndef __LOBBY_2_CLIENT_STEAM_H
#define __LOBBY_2_CLIENT_STEAM_H

#include "Lobby2Plugin.h"
#include "slikenet/DS_OrderedList.h"
#include "slikenet/types.h"

namespace SLNet
{
// This is a pure interface for Lobby2Client_SteamImpl
class RAK_DLL_EXPORT Lobby2Client_Steam : public SLNet::Lobby2Plugin
{
public:	
	// GetInstance() and DestroyInstance(instance*)
	STATIC_FACTORY_DECLARATIONS(Lobby2Client_Steam)

	virtual ~Lobby2Client_Steam() {}

	virtual void SendMsg(Lobby2Message *msg)=0;
	virtual void GetRoomMembers(DataStructures::OrderedList<uint64_t, uint64_t> &_roomMembers)=0;
	virtual const char * GetRoomMemberName(uint64_t memberId)=0;
	virtual bool IsRoomOwner(const uint64_t cSteamID)=0;
	virtual bool IsInRoom(void) const=0;
	virtual uint64_t GetNumRoomMembers(const uint64_t roomid)=0;
	virtual uint64_t GetMyUserID(void)=0;
	virtual const char* GetMyUserPersonalName(void)=0;
	virtual uint64_t GetRoomID(void) const=0;

protected:

};

};

#endif
