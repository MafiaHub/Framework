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

// Most of SLikeNet stuff was tried to be put here, but some of it had to go to CDemo.h too

#ifndef __RAKNET_ADDITIONS_FOR_IRRLICHT_DEMO_H
#define __RAKNET_ADDITIONS_FOR_IRRLICHT_DEMO_H

#include "slikenet/peerinterface.h"
#include "slikenet/ReplicaManager3.h"
#include "slikenet/NatPunchthroughClient.h"
#include "slikenet/CloudClient.h"
#include "slikenet/FullyConnectedMesh2.h"
#include "slikenet/UDPProxyClient.h"
#include "slikenet/TCPInterface.h"
#include "slikenet/HTTPConnection.h"
#include "../../Samples/PHPDirectoryServer2/PHPDirectoryServer2.h"
#include "vector3d.h"
#include "IAnimatedMeshSceneNode.h"
#include "slikenet/MessageIdentifiers.h"


class ReplicaManager3Irrlicht;
class CDemo;
class PlayerReplica;

// All externs defined in the corresponding CPP file
// Most of these classes has a manual entry, all of them have a demo
extern SLNet::RakPeerInterface *rakPeer; // Basic communication
extern SLNet::NetworkIDManager *networkIDManager; // Unique IDs per network object
extern ReplicaManager3Irrlicht *replicaManager3; // Autoreplicate network objects
extern SLNet::NatPunchthroughClient *natPunchthroughClient; // Connect peer to peer through routers
extern SLNet::CloudClient *cloudClient; // Used to upload game instance to the cloud
extern SLNet::FullyConnectedMesh2 *fullyConnectedMesh2; // Used to find out who is the session host
extern PlayerReplica *playerReplica; // Network object that represents the player

// A NAT punchthrough and proxy server Jenkins Software is hosting for free, should usually be online
#define DEFAULT_NAT_PUNCHTHROUGH_FACILITATOR_PORT 61111
#define DEFAULT_NAT_PUNCHTHROUGH_FACILITATOR_IP "natpunch.jenkinssoftware.com"

void InstantiateRakNetClasses(void);
void DeinitializeRakNetClasses(void);

// Base RakNet custom classes for Replica Manager 3, setup peer to peer networking
class BaseIrrlichtReplica : public SLNet::Replica3
{
public:
	BaseIrrlichtReplica();
	virtual ~BaseIrrlichtReplica();
	virtual SLNet::RM3ConstructionState QueryConstruction(SLNet::Connection_RM3 *destinationConnection, SLNet::ReplicaManager3 *replicaManager3) {return QueryConstruction_PeerToPeer(destinationConnection);}
	virtual bool QueryRemoteConstruction(SLNet::Connection_RM3 *sourceConnection) {return QueryRemoteConstruction_PeerToPeer(sourceConnection);}
	virtual void DeallocReplica(SLNet::Connection_RM3 *sourceConnection) {delete this;}
	virtual SLNet::RM3QuerySerializationResult QuerySerialization(SLNet::Connection_RM3 *destinationConnection) {return QuerySerialization_PeerToPeer(destinationConnection);}
	virtual void SerializeConstruction(SLNet::BitStream *constructionBitstream, SLNet::Connection_RM3 *destinationConnection);
	virtual bool DeserializeConstruction(SLNet::BitStream *constructionBitstream, SLNet::Connection_RM3 *sourceConnection);
	virtual SLNet::RM3SerializationResult Serialize(SLNet::SerializeParameters *serializeParameters);
	virtual void Deserialize(SLNet::DeserializeParameters *deserializeParameters);
	virtual void SerializeDestruction(SLNet::BitStream *destructionBitstream, SLNet::Connection_RM3 *destinationConnection) {}
	virtual bool DeserializeDestruction(SLNet::BitStream *destructionBitstream, SLNet::Connection_RM3 *sourceConnection) {return true;}
	virtual SLNet::RM3ActionOnPopConnection QueryActionOnPopConnection(SLNet::Connection_RM3 *droppedConnection) const {return QueryActionOnPopConnection_PeerToPeer(droppedConnection);}

	/// This function is not derived from Replica3, it's specific to this app
	/// Called from CDemo::UpdateRakNet
	virtual void Update(SLNet::TimeMS curTime);

	// Set when the object is constructed
	CDemo *demo;

	// real is written on the owner peer, read on the remote peer
	irr::core::vector3df position;
	SLNet::TimeMS creationTime;
};
// Game classes automatically updated by ReplicaManager3
class PlayerReplica : public BaseIrrlichtReplica, irr::scene::IAnimationEndCallBack
{
public:
	PlayerReplica();
	virtual ~PlayerReplica();
	// Every function below, before Update overriding a function in Replica3
	virtual void WriteAllocationID(SLNet::Connection_RM3 *destinationConnection, SLNet::BitStream *allocationIdBitstream) const;
	virtual void SerializeConstruction(SLNet::BitStream *constructionBitstream, SLNet::Connection_RM3 *destinationConnection);
	virtual bool DeserializeConstruction(SLNet::BitStream *constructionBitstream, SLNet::Connection_RM3 *sourceConnection);
	virtual SLNet::RM3SerializationResult Serialize(SLNet::SerializeParameters *serializeParameters);
	virtual void Deserialize(SLNet::DeserializeParameters *deserializeParameters);
	virtual void PostDeserializeConstruction(SLNet::BitStream *constructionBitstream, SLNet::Connection_RM3 *destinationConnection);
	virtual void PreDestruction(SLNet::Connection_RM3 *sourceConnection);

	virtual void Update(SLNet::TimeMS curTime);
	void UpdateAnimation(irr::scene::EMD2_ANIMATION_TYPE anim);
	float GetRotationDifference(float r1, float r2);
	virtual void OnAnimationEnd(irr::scene::IAnimatedMeshSceneNode* node);
	void PlayAttackAnimation(void);

	// playerName is only sent in SerializeConstruction, since it doesn't change
	SLNet::RakString playerName;

	// Networked rotation
	float rotationAroundYAxis;
	// Interpolation variables, not networked
	irr::core::vector3df positionDeltaPerMS;
	float rotationDeltaPerMS;
	SLNet::TimeMS interpEndTime, lastUpdate;

	// Updated based on the keypresses, to control remote animation
	bool isMoving;

	// Only instantiated for remote systems, you never see yourself
	irr::scene::IAnimatedMeshSceneNode* model;
	irr::scene::EMD2_ANIMATION_TYPE curAnim;

	// deathTimeout is set from the local player
	SLNet::TimeMS deathTimeout;
	bool IsDead(void) const;
	// isDead is set from network packets for remote players
	bool isDead;

	// List of all players, including our own
	static DataStructures::List<PlayerReplica*> playerList;
};
class BallReplica : public BaseIrrlichtReplica
{
public:
	BallReplica();
	virtual ~BallReplica();
	// Every function except update is overriding a function in Replica3
	virtual void WriteAllocationID(SLNet::Connection_RM3 *destinationConnection, SLNet::BitStream *allocationIdBitstream) const;
	virtual void SerializeConstruction(SLNet::BitStream *constructionBitstream, SLNet::Connection_RM3 *destinationConnection);
	virtual bool DeserializeConstruction(SLNet::BitStream *constructionBitstream, SLNet::Connection_RM3 *sourceConnection);
	virtual SLNet::RM3SerializationResult Serialize(SLNet::SerializeParameters *serializeParameters);
	virtual void Deserialize(SLNet::DeserializeParameters *deserializeParameters);
	virtual void PostDeserializeConstruction(SLNet::BitStream *constructionBitstream, SLNet::Connection_RM3 *destinationConnection);
	virtual void PreDestruction(SLNet::Connection_RM3 *sourceConnection);

	virtual void Update(SLNet::TimeMS curTime);

	// shotDirection is networked
	irr::core::vector3df shotDirection;

	// shotlifetime is calculated, not networked
	SLNet::TimeMS shotLifetime;
};
class Connection_RM3Irrlicht : public SLNet::Connection_RM3 {
public:
	Connection_RM3Irrlicht(const SLNet::SystemAddress &_systemAddress, SLNet::RakNetGUID _guid, CDemo *_demo) : SLNet::Connection_RM3(_systemAddress, _guid) {demo=_demo;}
	virtual ~Connection_RM3Irrlicht() {}

	virtual SLNet::Replica3 *AllocReplica(SLNet::BitStream *allocationId, SLNet::ReplicaManager3 *replicaManager3);
protected:
	CDemo *demo;
};

class ReplicaManager3Irrlicht : public SLNet::ReplicaManager3
{
public:
	virtual SLNet::Connection_RM3* AllocConnection(const SLNet::SystemAddress &systemAddress, SLNet::RakNetGUID rakNetGUID) const {
		return new Connection_RM3Irrlicht(systemAddress,rakNetGUID,demo);
	}
	virtual void DeallocConnection(SLNet::Connection_RM3 *connection) const {
		delete connection;
	}
	CDemo *demo;
};


#endif
