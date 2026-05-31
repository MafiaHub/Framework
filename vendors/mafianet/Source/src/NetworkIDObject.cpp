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
///


#include "mafianet/NetworkIDObject.h"
#include "mafianet/NetworkIDManager.h"
#include "mafianet/assert.h"
#include "mafianet/alloca.h"

using namespace MafiaNet;

NetworkIDObject::NetworkIDObject()
{
	networkID=UNASSIGNED_NETWORK_ID;
	parent=0;
	networkIDManager=0;
	nextInstanceForNetworkIDManager=0;
}
NetworkIDObject::~NetworkIDObject()
{
	if (networkIDManager)
		networkIDManager->StopTrackingNetworkIDObject(this);
}
void NetworkIDObject::SetNetworkIDManager( NetworkIDManager *manager)
{
	if (manager==networkIDManager)
		return;

	if (networkIDManager)
		networkIDManager->StopTrackingNetworkIDObject(this);

	networkIDManager=manager;
	if (networkIDManager==0)
	{
		networkID = UNASSIGNED_NETWORK_ID;
		return;
	}
	
	if (networkID == UNASSIGNED_NETWORK_ID)
	{
		// Prior ID not set
		networkID = networkIDManager->GetNewNetworkID();
	}

	networkIDManager->TrackNetworkIDObject(this);
}
NetworkIDManager * NetworkIDObject::GetNetworkIDManager( void ) const
{
	return networkIDManager;
}
NetworkID NetworkIDObject::GetNetworkID( void )
{
	return networkID;
}
void NetworkIDObject::SetNetworkID( NetworkID id )
{
	if (networkID==id)
		return;

	if ( id == UNASSIGNED_NETWORK_ID )
	{
		SetNetworkIDManager(0);
		return;
	}

	if ( networkIDManager )
		networkIDManager->StopTrackingNetworkIDObject(this);

	networkID = id;

	if (networkIDManager)
		networkIDManager->TrackNetworkIDObject(this);
}
void NetworkIDObject::SetParent( void *_parent )
{
	parent=_parent;
}
void* NetworkIDObject::GetParent( void ) const
{
	return parent;
}
