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

#ifndef __INTERVAL_TIMER_H
#define __INTERVAL_TIMER_H

#include "slikenet/types.h"

struct IntervalTimer
{
	void SetPeriod(SLNet::TimeMS period);
	bool UpdateInterval(SLNet::TimeMS elapsed);

	SLNet::TimeMS basePeriod, remaining;
};

#endif
