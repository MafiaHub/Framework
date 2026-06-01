/*
 *  Original work: Copyright (c) 2014, Oculus VR, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  RakNet License.txt file in the licenses directory of this source tree. An additional grant 
 *  of patent rights can be found in the RakNet Patents.txt file in the same directory.
 *
 *
 *  Modified work: Copyright (c) 2017-2018, SLikeSoft UG (haftungsbeschränkt)
 *
 *  This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
 *  license found in the license.txt file in the root directory of this source tree.
 */

#ifndef __RAK_THREAD_H
#define __RAK_THREAD_H

#include "Export.h"

namespace MafiaNet
{
/// To define a thread, use RAK_THREAD_DECLARATION(functionName);
#if defined(_WIN32)
#define RAK_THREAD_DECLARATION(functionName) unsigned __stdcall functionName( void* arguments )

#else
#define RAK_THREAD_DECLARATION(functionName) void* functionName( void* arguments )
#endif

class RAK_DLL_EXPORT RakThread
{
public:
	/// Create a thread, simplified to be cross platform without all the extra junk
	/// To then start that thread, call RakCreateThread(functionName, arguments);
	/// \param[in] start_address Function you want to call
	/// \param[in] arglist Arguments to pass to the function
	/// \return 0=success. >0 = error code

	/*
	nice value 	Win32 Priority
	-20 to -16 	THREAD_PRIORITY_HIGHEST
	-15 to -6 	THREAD_PRIORITY_ABOVE_NORMAL
	-5 to +4 	THREAD_PRIORITY_NORMAL
	+5 to +14 	THREAD_PRIORITY_BELOW_NORMAL
	+15 to +19 	THREAD_PRIORITY_LOWEST
	*/
#if defined(_WIN32)
	static int Create( unsigned __stdcall start_address( void* ), void *arglist, int priority=0);
#else
	static int Create( void* start_address( void* ), void *arglist, int priority=0);
#endif
};

}

#endif
