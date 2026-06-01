/*
 *  Original work: Copyright (c) 2014, Oculus VR, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  RakNet License.txt file in the licenses directory of this source tree. An additional grant 
 *  of patent rights can be found in the RakNet Patents.txt file in the same directory.
 *
 *
 *  Modified work: Copyright (c) 2017-2020, SLikeSoft UG (haftungsbeschränkt)
 *
 *  This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
 *  license found in the license.txt file in the root directory of this source tree.
 */

#include "mafianet/socket2.h"
#include "mafianet/memoryoverride.h"
#include "mafianet/assert.h"
#include "mafianet/sleep.h"
#include "mafianet/SocketDefines.h"
#include "mafianet/GetTime.h"
#include <stdio.h>
#include <string.h> // memcpy

using namespace MafiaNet;

#ifdef _WIN32
#else
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <errno.h>  // error numbers
#if !defined(ANDROID)
#include <ifaddrs.h>
#endif
#include <netinet/in.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#endif

#define RAKNET_SOCKET_2_INLINE_FUNCTIONS
#include "RakNetSocket2_Windows_Linux.cpp"
#include "RakNetSocket2_Windows_Linux_360.cpp"
#include "RakNetSocket2_Berkley.cpp"
#undef RAKNET_SOCKET_2_INLINE_FUNCTIONS

#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif

void RakNetSocket2Allocator::DeallocRNS2(RakNetSocket2 *s) { MafiaNet::OP_DELETE(s,_FILE_AND_LINE_);}
RakNetSocket2::RakNetSocket2() {eventHandler=0;}
RakNetSocket2::~RakNetSocket2() {}
void RakNetSocket2::SetRecvEventHandler(RNS2EventHandler *_eventHandler) {eventHandler=_eventHandler;}
RNS2Type RakNetSocket2::GetSocketType(void) const {return socketType;}
void RakNetSocket2::SetSocketType(RNS2Type t) {socketType=t;}
bool RakNetSocket2::IsBerkleySocket(void) const {
	return true; // All supported platforms use Berkeley sockets
}
SystemAddress RakNetSocket2::GetBoundAddress(void) const {return boundAddress;}

RakNetSocket2* RakNetSocket2Allocator::AllocRNS2(void)
{
	RakNetSocket2* s2;
#if defined(_WIN32)
	s2 = MafiaNet::OP_NEW<RNS2_Windows>(_FILE_AND_LINE_);
	s2->SetSocketType(RNS2T_WINDOWS);
#else
	s2 = MafiaNet::OP_NEW<RNS2_Linux>(_FILE_AND_LINE_);
	s2->SetSocketType(RNS2T_LINUX);
#endif
	return s2;
}
void RakNetSocket2::GetMyIP( SystemAddress addresses[MAXIMUM_NUMBER_OF_INTERNAL_IDS] )
{
#if defined(_WIN32)
	RNS2_Windows::GetMyIP( addresses );
#else
	RNS2_Linux::GetMyIP( addresses );
#endif
}

unsigned int RakNetSocket2::GetUserConnectionSocketIndex(void) const {return userConnectionSocketIndex;}
void RakNetSocket2::SetUserConnectionSocketIndex(unsigned int i) {userConnectionSocketIndex=i;}
RNS2EventHandler * RakNetSocket2::GetEventHandler(void) const {return eventHandler;}

void RakNetSocket2::DomainNameToIP( const char *domainName, char ip[65] ) {
	return DomainNameToIP_Berkley( domainName, ip );
}

bool IRNS2_Berkley::IsPortInUse(unsigned short port, const char *hostAddress, unsigned short addressFamily, int type ) {
	RNS2_BerkleyBindParameters bbp;
	bbp.remotePortRakNetWasStartedOn_PS3_PS4_PSP2=0;
	bbp.port=port; bbp.hostAddress=(char*) hostAddress;	bbp.addressFamily=addressFamily;
	bbp.type=type; bbp.protocol=0; bbp.nonBlockingSocket=false;
	bbp.setBroadcast=false;	bbp.doNotFragment=false; bbp.protocol=0;
	bbp.setIPHdrIncl=false;
	SystemAddress boundAddress;
	RNS2_Berkley *rns2 = (RNS2_Berkley*) RakNetSocket2Allocator::AllocRNS2();
	RNS2BindResult bindResult = rns2->Bind(&bbp, _FILE_AND_LINE_);
	RakNetSocket2Allocator::DeallocRNS2(rns2);
	return bindResult==BR_FAILED_TO_BIND_SOCKET;
}

#if defined(__APPLE__)
void SocketReadCallback(CFSocketRef s, CFSocketCallBackType type, CFDataRef address, const void *data, void *info)
// This C routine is called by CFSocket when there's data waiting on our 
// UDP socket.  It just redirects the call to Objective-C code.
{ }
#endif

RNS2BindResult RNS2_Berkley::BindShared( RNS2_BerkleyBindParameters *bindParameters, const char *file, unsigned int line ) {
	RNS2BindResult br;
#if RAKNET_SUPPORT_IPV6==1
	br=BindSharedIPV4And6(bindParameters, file, line);
#else
	br=BindSharedIPV4(bindParameters, file, line);
#endif

	if (br!=BR_SUCCESS)
		return br;

	unsigned long zero=0;
	RNS2_SendParameters bsp;
	bsp.data=(char*) &zero;
	bsp.length=4;
	bsp.systemAddress=boundAddress;
	bsp.ttl=0;
	RNS2SendResult sr = Send(&bsp, _FILE_AND_LINE_);
	if (sr<0)
		return BR_FAILED_SEND_TEST;

	memcpy(&binding, bindParameters, sizeof(RNS2_BerkleyBindParameters));

	/*
#if defined(__APPLE__)
	const CFSocketContext   context = { 0, this, nullptr, nullptr, nullptr };
	_cfSocket = CFSocketCreateWithNative(nullptr, rns2Socket, kCFSocketReadCallBack, SocketReadCallback, &context);
#endif
	*/

	return br;
}

RAK_THREAD_DECLARATION(RNS2_Berkley::RecvFromLoop)
{
	RNS2_Berkley *b = ( RNS2_Berkley * ) arguments;

	b->RecvFromLoopInt();
	return 0;
}
unsigned RNS2_Berkley::RecvFromLoopInt(void)
{
	isRecvFromLoopThreadActive.Increment();
	
	while ( endThreads == false )
	{
		RNS2RecvStruct *recvFromStruct;
		recvFromStruct=binding.eventHandler->AllocRNS2RecvStruct(_FILE_AND_LINE_);
		if (recvFromStruct != nullptr)
		{
			recvFromStruct->socket=this;
			RecvFromBlocking(recvFromStruct);

			if (recvFromStruct->bytesRead>0)
			{
				RakAssert(recvFromStruct->systemAddress.GetPort());
				binding.eventHandler->OnRNS2Recv(recvFromStruct);
			}
			else
			{
				RakSleep(0);
				binding.eventHandler->DeallocRNS2RecvStruct(recvFromStruct, _FILE_AND_LINE_);
			}
		}
	}
	isRecvFromLoopThreadActive.Decrement();

	return 0;
}
RNS2_Berkley::RNS2_Berkley()
{
	rns2Socket=(RNS2Socket)INVALID_SOCKET;
}
RNS2_Berkley::~RNS2_Berkley()
{
	if (rns2Socket!=INVALID_SOCKET)
	{
		/*
#if defined(__APPLE__)
		CFSocketInvalidate(_cfSocket);
#endif
		*/

		closesocket__(rns2Socket);
	}

}
int RNS2_Berkley::CreateRecvPollingThread(int threadPriority)
{
	endThreads=false;

	int errorCode = MafiaNet::RakThread::Create(RecvFromLoop, this, threadPriority);
	return errorCode;
}
void RNS2_Berkley::SignalStopRecvPollingThread(void)
{
	endThreads=true;
}
void RNS2_Berkley::BlockOnStopRecvPollingThread(void)
{
	endThreads=true;

	// Get recvfrom to unblock
	RNS2_SendParameters bsp;
	unsigned long zero=0;
	bsp.data=(char*) &zero;
	bsp.length=4;
	bsp.systemAddress=boundAddress;
	bsp.ttl=0;
	Send(&bsp, _FILE_AND_LINE_);

	MafiaNet::TimeMS timeout = MafiaNet::GetTimeMS()+1000;
	while ( isRecvFromLoopThreadActive.GetValue()>0 && MafiaNet::GetTimeMS()<timeout )
	{
		// Get recvfrom to unblock
		Send(&bsp, _FILE_AND_LINE_);
		RakSleep(30);
	}
}
const RNS2_BerkleyBindParameters *RNS2_Berkley::GetBindings(void) const {return &binding;}
RNS2Socket RNS2_Berkley::GetSocket(void) const {return rns2Socket;}
// See RakNetSocket2_Berkley.cpp for WriteSharedIPV4, BindSharedIPV4And6 and other implementations

#if   defined(_WIN32)
RNS2_Windows::RNS2_Windows() {slo=0;}
RNS2_Windows::~RNS2_Windows() {}
RNS2BindResult RNS2_Windows::Bind( RNS2_BerkleyBindParameters *bindParameters, const char *file, unsigned int line ) {
	RNS2BindResult bindResult = BindShared(bindParameters, file, line);
	if (bindResult == BR_FAILED_TO_BIND_SOCKET)
	{
		// Sometimes windows will fail if the socket is recreated too quickly
		RakSleep(100);
		bindResult = BindShared(bindParameters, file, line);
	}
	return bindResult;
}
RNS2SendResult RNS2_Windows::Send( RNS2_SendParameters *sendParameters, const char *file, unsigned int line ) {
	if (slo)
	{
		RNS2SendResult len;
		len = slo->RakNetSendTo(sendParameters->data, sendParameters->length,sendParameters->systemAddress);
		if (len>=0)
			return len;
	} 
	return Send_Windows_Linux_360NoVDP(rns2Socket,sendParameters, file, line);
}
void RNS2_Windows::GetMyIP( SystemAddress addresses[MAXIMUM_NUMBER_OF_INTERNAL_IDS] ) {return GetMyIP_Windows_Linux(addresses);}
void RNS2_Windows::SetSocketLayerOverride(SocketLayerOverride *_slo) {slo = _slo;}
SocketLayerOverride* RNS2_Windows::GetSocketLayerOverride(void) {return slo;}
#else
RNS2BindResult RNS2_Linux::Bind( RNS2_BerkleyBindParameters *bindParameters, const char *file, unsigned int line ) {return BindShared(bindParameters, file, line);}
RNS2SendResult RNS2_Linux::Send( RNS2_SendParameters *sendParameters, const char *file, unsigned int line ) {return Send_Windows_Linux_360NoVDP(rns2Socket,sendParameters, file, line);}
void RNS2_Linux::GetMyIP( SystemAddress addresses[MAXIMUM_NUMBER_OF_INTERNAL_IDS] ) {return GetMyIP_Windows_Linux(addresses);}
#endif // Linux
