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

#ifndef __SQL_LITE_3_PLUGIN_COMMON_H
#define __SQL_LITE_3_PLUGIN_COMMON_H

#include "slikenet/DS_Multilist.h"
#include "slikenet/string.h"
#include "slikenet/BitStream.h"

/// \defgroup SQL_LITE_3_PLUGIN SQLite3Plugin
/// \brief Code to transmit SQLite3 commands across the network
/// \details
/// \ingroup PLUGINS_GROUP

/// Contains a result row, which is just an array of strings
/// \ingroup SQL_LITE_3_PLUGIN
struct SQLite3Row
{
	DataStructures::List<SLNet::RakString> entries;
};

/// Contains a result table, which is an array of column name strings, followed by an array of SQLite3Row
/// \ingroup SQL_LITE_3_PLUGIN
struct SQLite3Table
{
	SQLite3Table();
	~SQLite3Table();
	void Serialize(SLNet::BitStream *bitStream);
	void Deserialize(SLNet::BitStream *bitStream);

	DataStructures::List<SLNet::RakString> columnNames;
	DataStructures::List<SQLite3Row*> rows;
};

#endif
