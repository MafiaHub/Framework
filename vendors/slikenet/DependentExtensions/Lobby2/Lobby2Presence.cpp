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

#include "Lobby2Presence.h"
#include "slikenet/BitStream.h"

using namespace SLNet;


Lobby2Presence::Lobby2Presence() {
	status=UNDEFINED;
	isVisible=true;
}
Lobby2Presence::Lobby2Presence(const Lobby2Presence& input) {
	status=input.status;
	isVisible=input.isVisible;
	titleNameOrID=input.titleNameOrID;



	statusString=input.statusString;
}
Lobby2Presence& Lobby2Presence::operator = ( const Lobby2Presence& input )
{
	status=input.status;
	isVisible=input.isVisible;
	titleNameOrID=input.titleNameOrID;



	statusString=input.statusString;
	return *this;
}

Lobby2Presence::~Lobby2Presence()
{

}
void Lobby2Presence::Serialize(SLNet::BitStream *bitStream, bool writeToBitstream)
{
	unsigned char gs = (unsigned char) status;
	bitStream->Serialize(writeToBitstream,gs);
	status=(Status) gs;
	bitStream->Serialize(writeToBitstream,isVisible);
	bitStream->Serialize(writeToBitstream,titleNameOrID);



	bitStream->Serialize(writeToBitstream,statusString);
}
