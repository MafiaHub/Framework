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

#ifndef __LOBBY_2_SERVER_PGSQL_H
#define __LOBBY_2_SERVER_PGSQL_H

#include "Lobby2Server.h"

class PostgreSQLInterface;

namespace SLNet
{

/// PostgreSQL specific functionality to the lobby server
class RAK_DLL_EXPORT Lobby2Server_PGSQL : public SLNet::Lobby2Server
{
public:	
	Lobby2Server_PGSQL();
	virtual ~Lobby2Server_PGSQL();

	STATIC_FACTORY_DECLARATIONS(Lobby2Server_PGSQL)
	
	/// ConnectTo to the database \a numWorkerThreads times using the connection string
	/// \param[in] conninfo See the postgre docs
	/// \return True on success, false on failure.
	virtual bool ConnectToDB(const char *conninfo, int numWorkerThreads);

	/// Add input to the worker threads, from a thread already running
	virtual void AddInputFromThread(Lobby2Message *msg, unsigned int targetUserId, SLNet::RakString targetUserHandle);
	/// Add output from the worker threads, from a thread already running. This is in addition to the current message, so is used for notifications
	virtual void AddOutputFromThread(Lobby2Message *msg, unsigned int targetUserId, SLNet::RakString targetUserHandle);

protected:

	virtual void AddInputCommand(Lobby2ServerCommand command);
	virtual void* PerThreadFactory(void *context);
	virtual void PerThreadDestructor(void* factoryResult, void *context);
	virtual void ClearConnections(void);
	DataStructures::List<PostgreSQLInterface *> connectionPool;
};
	
}

#endif
