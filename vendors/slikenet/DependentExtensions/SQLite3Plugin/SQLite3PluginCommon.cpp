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

#include "SQLite3PluginCommon.h"


SQLite3Table::SQLite3Table()
{

}
SQLite3Table::~SQLite3Table()
{
	for (unsigned int i=0; i < rows.Size(); i++)
		SLNet::OP_DELETE(rows[i],_FILE_AND_LINE_);
}

void SQLite3Table::Serialize(SLNet::BitStream *bitStream)
{
	bitStream->Write(columnNames.Size());
	unsigned int idx1, idx2;
	for (idx1=0; idx1 < columnNames.Size(); idx1++)
		bitStream->Write(columnNames[idx1]);
	bitStream->Write(rows.Size());
	for (idx1=0; idx1 < rows.Size(); idx1++)
	{
		for (idx2=0; idx2 < rows[idx1]->entries.Size(); idx2++)
		{
			bitStream->Write(rows[idx1]->entries[idx2]);
		}
	}
}
void SQLite3Table::Deserialize(SLNet::BitStream *bitStream)
{
	for (unsigned int i=0; i < rows.Size(); i++)
		SLNet::OP_DELETE(rows[i],_FILE_AND_LINE_);
	rows.Clear(true,_FILE_AND_LINE_);
	columnNames.Clear(true , _FILE_AND_LINE_ );

	unsigned int numColumns, numRows;
	bitStream->Read(numColumns);
	unsigned int idx1,idx2;
	SLNet::RakString inputStr;
	for (idx1=0; idx1 < numColumns; idx1++)
	{
		bitStream->Read(inputStr);
		columnNames.Push(inputStr, _FILE_AND_LINE_ );
	}
	bitStream->Read(numRows);
	for (idx1=0; idx1 < numRows; idx1++)
	{
		SQLite3Row *row = SLNet::OP_NEW<SQLite3Row>(_FILE_AND_LINE_);
		rows.Push(row,_FILE_AND_LINE_);
		for (idx2=0; idx2 < numColumns; idx2++)
		{
			bitStream->Read(inputStr);
			row->entries.Push(inputStr, _FILE_AND_LINE_ );
		}
	}
}
