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

#include "SQLite3ServerPlugin.h"
#include "slikenet/MessageIdentifiers.h"
#include "slikenet/BitStream.h"
#include "slikenet/GetTime.h"

using namespace SLNet;

bool operator<( const DataStructures::MLKeyRef<SLNet::RakString> &inputKey, const SQLite3ServerPlugin::NamedDBHandle &cls ) {return inputKey.Get() < cls.dbIdentifier;}
bool operator>( const DataStructures::MLKeyRef<SLNet::RakString> &inputKey, const SQLite3ServerPlugin::NamedDBHandle &cls ) {return inputKey.Get() > cls.dbIdentifier;}
bool operator==( const DataStructures::MLKeyRef<SLNet::RakString> &inputKey, const SQLite3ServerPlugin::NamedDBHandle &cls ) {return inputKey.Get() == cls.dbIdentifier;}


int PerRowCallback(void *userArgument, int argc, char **argv, char **azColName)
{
	SQLite3Table *outputTable = (SQLite3Table*)userArgument;
	unsigned int idx;
	if (outputTable->columnNames.Size()==0)
	{
		for (idx=0; idx < (unsigned int) argc; idx++)
			outputTable->columnNames.Push(azColName[idx], _FILE_AND_LINE_ );
	}
	SQLite3Row *row = SLNet::OP_NEW<SQLite3Row>(_FILE_AND_LINE_);
	outputTable->rows.Push(row,_FILE_AND_LINE_);
	for (idx=0; idx < (unsigned int) argc; idx++)
	{
		if (argv[idx])
			row->entries.Push(argv[idx], _FILE_AND_LINE_ );
		else
			row->entries.Push("", _FILE_AND_LINE_ );
	}
	return 0;
}
SQLite3ServerPlugin::SQLite3ServerPlugin()
{
}
SQLite3ServerPlugin::~SQLite3ServerPlugin()
{
	StopThreads();
}
bool SQLite3ServerPlugin::AddDBHandle(SLNet::RakString dbIdentifier, sqlite3 *dbHandle, bool dbAutoCreated)
{
	if (dbIdentifier.IsEmpty())
		return false;
	unsigned int idx = dbHandles.GetInsertionIndex(dbIdentifier);
	if (idx==(unsigned int)-1)
		return false;
	NamedDBHandle ndbh;
	ndbh.dbHandle=dbHandle;
	ndbh.dbIdentifier=dbIdentifier;
	ndbh.dbAutoCreated=dbAutoCreated;
	ndbh.whenCreated= SLNet::GetTimeMS();
	dbHandles.InsertAtIndex(ndbh,idx,_FILE_AND_LINE_);
	
#ifdef SQLite3_STATEMENT_EXECUTE_THREADED
	if (sqlThreadPool.WasStarted()==false)
		sqlThreadPool.StartThreads(1,0);
#endif

	return true;
}
void SQLite3ServerPlugin::RemoveDBHandle(SLNet::RakString dbIdentifier, bool alsoCloseConnection)
{
	unsigned int idx = dbHandles.GetIndexOf(dbIdentifier);
	if (idx!=(unsigned int)-1)
	{
		if (alsoCloseConnection)
		{
			printf("Closed %s\n", dbIdentifier.C_String());
			sqlite3_close(dbHandles[idx].dbHandle);
		}
		dbHandles.RemoveAtIndex(idx,_FILE_AND_LINE_);
#ifdef SQLite3_STATEMENT_EXECUTE_THREADED
	if (dbHandles.GetSize()==0)
		StopThreads();
#endif // SQLite3_STATEMENT_EXECUTE_THREADED
	}
}
void SQLite3ServerPlugin::RemoveDBHandle(sqlite3 *dbHandle, bool alsoCloseConnection)
{
	unsigned int idx;
	for (idx=0; idx < dbHandles.GetSize(); idx++)
	{
		if (dbHandles[idx].dbHandle==dbHandle)
		{
			if (alsoCloseConnection)
			{
				printf("Closed %s\n", dbHandles[idx].dbIdentifier.C_String());
				sqlite3_close(dbHandles[idx].dbHandle);
			}
			dbHandles.RemoveAtIndex(idx,_FILE_AND_LINE_);
#ifdef SQLite3_STATEMENT_EXECUTE_THREADED
			if (dbHandles.GetSize()==0)
				StopThreads();
#endif // SQLite3_STATEMENT_EXECUTE_THREADED
			return;
		}
	}
}
#ifdef SQLite3_STATEMENT_EXECUTE_THREADED
void SQLite3ServerPlugin::Update(void)
{
	SQLExecThreadOutput output;
	while (sqlThreadPool.HasOutputFast() && sqlThreadPool.HasOutput())
	{
		output = sqlThreadPool.GetOutput();
		SLNet::BitStream bsOut((unsigned char*) output.data, output.length,false);
		SendUnified(&bsOut, MEDIUM_PRIORITY,RELIABLE_ORDERED,0,output.sender,false);
		rakFree_Ex(output.data,_FILE_AND_LINE_);
	}
}
SQLite3ServerPlugin::SQLExecThreadOutput ExecStatementThread(SQLite3ServerPlugin::SQLExecThreadInput threadInput, bool *returnOutput, void* perThreadData)
{
	unsigned int queryId;
	SLNet::RakString dbIdentifier;
	SLNet::RakString inputStatement;
	SLNet::BitStream bsIn((unsigned char*) threadInput.data, threadInput.length, false);
	bsIn.IgnoreBytes(sizeof(MessageID));
	bsIn.Read(queryId);
	bsIn.Read(dbIdentifier);
	bsIn.Read(inputStatement);
	// bool isRequest;
	// bsIn.Read(isRequest);
	bsIn.IgnoreBits(1);

	char *errorMsg;
	SLNet::RakString errorMsgStr;
	SQLite3Table outputTable;					
	sqlite3_exec(threadInput.dbHandle, inputStatement.C_String(), PerRowCallback, &outputTable, &errorMsg);
	if (errorMsg)
	{
		errorMsgStr=errorMsg;
		sqlite3_free(errorMsg);
	}

	SLNet::BitStream bsOut;
	bsOut.Write((MessageID)ID_SQLite3_EXEC);
	bsOut.Write(queryId);
	bsOut.Write(dbIdentifier);
	bsOut.Write(inputStatement);
	bsOut.Write(false);
	bsOut.Write(errorMsgStr);
	outputTable.Serialize(&bsOut);

	// Free input data
	rakFree_Ex(threadInput.data,_FILE_AND_LINE_);

	// Copy to output data
	SQLite3ServerPlugin::SQLExecThreadOutput threadOutput;
	threadOutput.data=(char*) rakMalloc_Ex(bsOut.GetNumberOfBytesUsed(),_FILE_AND_LINE_);
	memcpy(threadOutput.data,bsOut.GetData(),bsOut.GetNumberOfBytesUsed());
	threadOutput.length=bsOut.GetNumberOfBytesUsed();
	threadOutput.sender=threadInput.sender;	
	// SendUnified(&bsOut, MEDIUM_PRIORITY,RELIABLE_ORDERED,0,packet->systemAddress,false);

	*returnOutput=true;
	return threadOutput;
}
#endif // SQLite3_STATEMENT_EXECUTE_THREADED

PluginReceiveResult SQLite3ServerPlugin::OnReceive(Packet *packet)
{
	switch (packet->data[0])
	{
	case ID_SQLite3_EXEC:
		{
			unsigned int queryId;
			SLNet::RakString dbIdentifier;
			SLNet::RakString inputStatement;
			SLNet::BitStream bsIn(packet->data, packet->length, false);
			bsIn.IgnoreBytes(sizeof(MessageID));
			bsIn.Read(queryId);
			bsIn.Read(dbIdentifier);
			bsIn.Read(inputStatement);
			bool isRequest;
			bsIn.Read(isRequest);
			if (isRequest)
			{
				// Server code

				unsigned int idx = dbHandles.GetIndexOf(dbIdentifier);
				if (idx==-1)
				{
					SLNet::BitStream bsOut;
					bsOut.Write((MessageID)ID_SQLite3_UNKNOWN_DB);
					bsOut.Write(queryId);
					bsOut.Write(dbIdentifier);
					bsOut.Write(inputStatement);
					SendUnified(&bsOut, MEDIUM_PRIORITY,RELIABLE_ORDERED,0,packet->systemAddress,false);
				}
				else
				{
#ifdef SQLite3_STATEMENT_EXECUTE_THREADED
					// Push to the thread
					SQLExecThreadInput input;
					input.data=(char*) rakMalloc_Ex(packet->length, _FILE_AND_LINE_);
					memcpy(input.data,packet->data,packet->length);
					input.dbHandle=dbHandles[idx].dbHandle;
					input.length=packet->length;
					input.sender=packet->systemAddress;
					sqlThreadPool.AddInput(ExecStatementThread, input);
#else
					char *errorMsg;
					SLNet::RakString errorMsgStr;
					SQLite3Table outputTable;					
					sqlite3_exec(dbHandles[idx].dbHandle, inputStatement.C_String(), PerRowCallback, &outputTable, &errorMsg);
					if (errorMsg)
					{
						errorMsgStr=errorMsg;
						sqlite3_free(errorMsg);
					}
					SLNet::BitStream bsOut;
					bsOut.Write((MessageID)ID_SQLite3_EXEC);
					bsOut.Write(queryId);
					bsOut.Write(dbIdentifier);
					bsOut.Write(inputStatement);
					bsOut.Write(false);
					bsOut.Write(errorMsgStr);
					outputTable.Serialize(&bsOut);
					SendUnified(&bsOut, MEDIUM_PRIORITY,RELIABLE_ORDERED,0,packet->systemAddress,false);
#endif
				}
			}
			return RR_STOP_PROCESSING_AND_DEALLOCATE;
		}
		break;
	}

	return RR_CONTINUE_PROCESSING;
}

void SQLite3ServerPlugin::OnAttach(void)
{
}
void SQLite3ServerPlugin::OnDetach(void)
{
	StopThreads();
}
void SQLite3ServerPlugin::StopThreads(void)
{
#ifdef SQLite3_STATEMENT_EXECUTE_THREADED
	sqlThreadPool.StopThreads();
	unsigned int i;
	for (i=0; i < sqlThreadPool.InputSize(); i++)
	{
		SLNet::OP_DELETE(sqlThreadPool.GetInputAtIndex(i).data, _FILE_AND_LINE_);
	}
	sqlThreadPool.ClearInput();
	for (i=0; i < sqlThreadPool.OutputSize(); i++)
	{
		SLNet::OP_DELETE(sqlThreadPool.GetOutputAtIndex(i).data, _FILE_AND_LINE_);
	}
	sqlThreadPool.ClearOutput();
#endif
}
