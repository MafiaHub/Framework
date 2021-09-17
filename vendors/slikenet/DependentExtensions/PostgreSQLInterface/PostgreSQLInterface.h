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

#ifndef __POSTGRESQL_INTERFACE_H
#define __POSTGRESQL_INTERFACE_H

struct pg_conn;
typedef struct pg_conn PGconn;

struct pg_result;
typedef struct pg_result PGresult;

#include "slikenet/string.h"
#include "slikenet/DS_OrderedList.h"

class PostgreSQLInterface
{
public:
	PostgreSQLInterface();
	virtual ~PostgreSQLInterface();

	/// Connect to the database using the connection string
	/// \param[in] conninfo See the postgre docs
	/// \return True on success, false on failure.
	bool Connect(const char *conninfo);

	/// Use a connection allocated elsewehre
	void AssignConnection(PGconn *_pgConn);

	/// Get the instance of PGconn
	PGconn *GetPGConn(void) const;

	/// Disconnect from the database
	void Disconnect(void);

	/// If any of the above functions fail, the error string is stored internally.  Call this to get it.
	virtual const char *GetLastError(void) const;

	// Returns  DEFAULT EXTRACT(EPOCH FROM current_timestamp))
	long long GetEpoch(void);

	// Returns a string containing LOCALTIMESTAMP on the server
	char *GetLocalTimestamp(void);

	static bool PQGetValueFromBinary(int *output, PGresult *result, unsigned int rowIndex, const char *columnName);
	static bool PQGetValueFromBinary(int *output, PGresult *result, int rowIndex, const char *columnName);
	static bool PQGetValueFromBinary(unsigned int *output, PGresult *result, int rowIndex, const char *columnName);
	static bool PQGetValueFromBinary(long long *output, PGresult *result, int rowIndex, const char *columnName);
	static bool PQGetValueFromBinary(float *output, PGresult *result, int rowIndex, const char *columnName);
	static bool PQGetValueFromBinary(double *output, PGresult *result, int rowIndex, const char *columnName);
	static bool PQGetValueFromBinary(bool *output, PGresult *result, int rowIndex, const char *columnName);
	static bool PQGetValueFromBinary(SLNet::RakString *output, PGresult *result, int rowIndex, const char *columnName);
	static bool PQGetValueFromBinary(char **output, unsigned int *outputLength, PGresult *result, int rowIndex, const char *columnName);
	static bool PQGetValueFromBinary(char **output, int *outputLength, PGresult *result, int rowIndex, const char *columnName);

	static void EncodeQueryInput(const char *colName, unsigned int value, SLNet::RakString &paramTypeStr, SLNet::RakString &valueStr, int &numParams, char **paramData, int *paramLength, int *paramFormat);
	static void EncodeQueryInput(const char *colName, bool value, SLNet::RakString &paramTypeStr, SLNet::RakString &valueStr, int &numParams, char **paramData, int *paramLength, int *paramFormat);
	static void EncodeQueryInput(const char *colName, int value, SLNet::RakString &paramTypeStr, SLNet::RakString &valueStr, int &numParams, char **paramData, int *paramLength, int *paramFormat);
	static void EncodeQueryInput(const char *colName, float value, SLNet::RakString &paramTypeStr, SLNet::RakString &valueStr, int &numParams, char **paramData, int *paramLength, int *paramFormat);
	static void EncodeQueryInput(const char *colName, char *binaryData, int binaryDataLength, SLNet::RakString &paramTypeStr, SLNet::RakString &valueStr, int &numParams, char **paramData, int *paramLength, int *paramFormat, bool writeEmpty);
	static void EncodeQueryInput(const char *colName, const char *str, SLNet::RakString &paramTypeStr, SLNet::RakString &valueStr, int &numParams, char **paramData, int *paramLength, int *paramFormat, bool writeEmpty, const char *type = "text");
	static void EncodeQueryInput(const char *colName, const SLNet::RakString &str, SLNet::RakString &paramTypeStr, SLNet::RakString &valueStr, int &numParams, char **paramData, int *paramLength, int *paramFormat, bool writeEmpty, const char *type = "text");

	static void EncodeQueryUpdate(const char *colName, unsigned int value, SLNet::RakString &valueStr, int &numParams, char **paramData, int *paramLength, int *paramFormat);
	static void EncodeQueryUpdate(const char *colName, int value, SLNet::RakString &valueStr, int &numParams, char **paramData, int *paramLength, int *paramFormat);
	static void EncodeQueryUpdate(const char *colName, float value, SLNet::RakString &valueStr, int &numParams, char **paramData, int *paramLength, int *paramFormat);
	static void EncodeQueryUpdate(const char *colName, char *binaryData, int binaryDataLength, SLNet::RakString &valueStr, int &numParams, char **paramData, int *paramLength, int *paramFormat);
	static void EncodeQueryUpdate(const char *colName, const char *str, SLNet::RakString &valueStr, int &numParams, char **paramData, int *paramLength, int *paramFormat, const char *type = "text");
	static void EncodeQueryUpdate(const char *colName, const SLNet::RakString &str, SLNet::RakString &valueStr, int &numParams, char **paramData, int *paramLength, int *paramFormat, const char *type = "text");

	// Standard query
	PGresult * QueryVariadic( const char *input, ... );
	static void ClearResult(PGresult *result);

	// Pass queries to the server
	bool ExecuteBlockingCommand(const char *command, PGresult **result, bool rollbackOnFailure);
	bool IsResultSuccessful(PGresult *result, bool rollbackOnFailure);
	void Rollback(void);
	static void EndianSwapInPlace(char* data, int dataLength);
	SLNet::RakString GetEscapedString(const char *input) const;
protected:	

	PGconn *pgConn;
	bool pgConnAllocatedHere;
	bool isConnected;
	char lastError[1024];

	// Connection parameters
	SLNet::RakString _conninfo;

	//	DataStructures::List<SLNet::RakString> preparedStatements;
	DataStructures::List<SLNet::RakString> preparedQueries;
};

#endif
