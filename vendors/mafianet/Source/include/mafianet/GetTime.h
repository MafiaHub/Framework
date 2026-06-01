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

/// \file GetTime.h
/// \brief Returns the value from QueryPerformanceCounter.  This is the function RakNet uses to represent time. This time won't match the time returned by GetTimeCount(). See http://www.jenkinssoftware.com/forum/index.php?topic=2798.0
///


#ifndef __GET_TIME_H
#define __GET_TIME_H

#include "Export.h"
#include "time.h" // For MafiaNet::TimeMS

namespace MafiaNet
{
	/// Same as GetTimeMS
	/// Holds the time in either a 32 or 64 bit variable, depending on __GET_TIME_64BIT
	MafiaNet::Time RAK_DLL_EXPORT GetTime( void );

	/// Return the time as 32 bit
	/// \note The maximum delta between returned calls is 1 second - however, RakNet calls this constantly anyway. See NormalizeTime() in the cpp.
	MafiaNet::TimeMS RAK_DLL_EXPORT GetTimeMS( void );
	
	/// Return the time as 64 bit
	/// \note The maximum delta between returned calls is 1 second - however, RakNet calls this constantly anyway. See NormalizeTime() in the cpp.
	MafiaNet::TimeUS RAK_DLL_EXPORT GetTimeUS( void );

	/// a > b?
	extern RAK_DLL_EXPORT bool GreaterThan(MafiaNet::Time a, MafiaNet::Time b);
	/// a < b?
	extern RAK_DLL_EXPORT bool LessThan(MafiaNet::Time a, MafiaNet::Time b);
}

#endif
