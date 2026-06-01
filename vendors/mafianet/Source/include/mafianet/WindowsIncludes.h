/*
 *  Original work: Copyright (c) 2014, Oculus VR, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  RakNet License.txt file in the licenses directory of this source tree. An additional grant 
 *  of patent rights can be found in the RakNet Patents.txt file in the same directory.
 *
 *
 *  Modified work: Copyright (c) 2016-2019, SLikeSoft UG (haftungsbeschränkt)
 *
 *  This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
 *  license found in the license.txt file in the root directory of this source tree.
 */

#ifndef NOMINMAX
	#define NOMINMAX
#endif

#if defined (_WIN32)
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <IPHlpApi.h> // used for GetAdaptersAddresses()
#pragma comment(lib, "IPHLPAPI.lib") // used for GetAdaptersAddresses()
#endif
