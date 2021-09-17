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

#ifndef __MY_SQL_INTERFACE_H
#define __MY_SQL_INTERFACE_H

#include "slikenet/string.h"

struct st_mysql_res;
struct st_mysql;

class MySQLInterface
{
public:
	MySQLInterface();
	virtual ~MySQLInterface();

	/// Calls mysql_real_connect with the implicit mySqlConnection 
	bool Connect (const char *host,
		const char *user,
		const char *passwd,
		const char *db,
		unsigned int port,
		const char *unix_socket,
		unsigned long clientflag);

	/// Disconnect from the database
	void Disconnect(void);

	/// Returns if we are connected to the database
	bool IsConnected(void) const;

	/// If any of the above functions fail, the error string is stored internally.  Call this to get it.
	virtual const char *GetLastError(void) const;

	/// Returns the result of SELECT LOCALTIMESTAMP
	char *GetLocalTimestamp(void);

protected:
	// Pass queries to the server
	bool ExecuteBlockingCommand(const char *command);
	bool ExecuteBlockingCommand(const char *command, st_mysql_res **result, bool rollbackOnFailure = false);
	bool ExecuteQueryReadInt (const char * query, int *value);
	void Commit(void);
	void Rollback(void);
	SLNet::RakString GetEscapedString(const char *input) const;

	st_mysql *mySqlConnection;
	char lastError[1024];

	// Copy of connection parameters
	SLNet::RakString _host;
	SLNet::RakString _user;
	SLNet::RakString _passwd;
	SLNet::RakString _db;
	unsigned int _port;
	SLNet::RakString _unix_socket;
	unsigned long _clientflag;

};

#endif
