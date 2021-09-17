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
/// \brief Writes RakNetStatistics for all connected systems once per second to SQLiteClientLogger
///



#ifndef __SQL_LITE_CLIENT_LOGGER_RAKNET_STATISTICS_H_
#define __SQL_LITE_CLIENT_LOGGER_RAKNET_STATISTICS_H_

#include "slikenet/PluginInterface2.h"

namespace SLNet
{
	/// \ingroup PACKETLOGGER_GROUP
	/// \brief Packetlogger that outputs to a file
	class RAK_DLL_EXPORT SQLiteClientLogger_RakNetStatistics : public PluginInterface2
	{
	public:
		SQLiteClientLogger_RakNetStatistics();
		virtual ~SQLiteClientLogger_RakNetStatistics();
		virtual void Update(void);
	protected:
		SLNet::TimeUS lastUpdate;
	};
}

#endif
