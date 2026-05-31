/*
 *  Original work: Copyright (c) 2014, Oculus VR, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  RakNet License.txt file in the licenses directory of this source tree. An additional grant 
 *  of patent rights can be found in the RakNet Patents.txt file in the same directory.
 *
 *
 *  Modified work: Copyright (c) 2016-2020, SLikeSoft UG (haftungsbeschränkt)
 *
 *  This source code was modified by SLikeSoft. Modifications are licensed under the MIT-style
 *  license found in the license.txt file in the root directory of this source tree.
 */

#include "mafianet/EmptyHeader.h"

#ifdef RAKNET_SOCKET_2_INLINE_FUNCTIONS

#ifndef RAKNETSOCKET2_BERKLEY_CPP
#define RAKNETSOCKET2_BERKLEY_CPP

// Berkeley socket implementation for Windows and Linux

#ifdef _WIN32
#include <tchar.h>	// used for _tprintf() (via RAKNET_DEBUG_TPRINTF)
#else
#include "mafianet/LinuxStrings.h" // used for _stricmp()
#include <sys/types.h>             // used for getaddrinfo()
#include <sys/socket.h>            // used for getaddrinfo()
#include <netdb.h>                 // used for getaddrinfo()
#endif

#include "mafianet/Itoa.h"
#include "mafianet/WSAStartupSingleton.h"

// Domain name resolution functions
void DomainNameToIP_Berkley_IPV4And6( const char *domainName, char ip[65] )
{
#if RAKNET_SUPPORT_IPV6==1
	struct addrinfo hints, *res, *p;
	int status;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	if ((status = getaddrinfo(domainName, nullptr, &hints, &res)) != 0) {
		ip[0] = '\0';
		return;
	}

	p=res;
	void *addr;
	if (p->ai_family == AF_INET)
	{
		struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
		addr = &(ipv4->sin_addr);
		inet_ntop(AF_INET, &ipv4->sin_addr, ip, 65);
	}
	else
	{
		struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
		addr = &(ipv6->sin6_addr);
		getnameinfo((struct sockaddr *)ipv6, sizeof(struct sockaddr_in6), ip, 65, nullptr, 0, NI_NUMERICHOST);
	}
	freeaddrinfo(res);
#else
	(void) domainName;
	(void) ip;
#endif
}

void DomainNameToIP_Berkley_IPV4( const char *domainName, char ip[65] )
{
	struct addrinfo *addressinfo = nullptr;
	struct addrinfo *originalAddressInfo = nullptr;
	WSAStartupSingleton::AddRef();
	int error = getaddrinfo(domainName, nullptr, nullptr, &addressinfo);
	WSAStartupSingleton::Deref();

	if ( error != 0 || addressinfo == 0 )
	{
		ip[0] = '\0';
		return;
	}

	originalAddressInfo = addressinfo;

	while (addressinfo != nullptr) {
		if (addressinfo->ai_family == AF_INET) {
			break;
		}
		addressinfo = addressinfo->ai_next;
	}

	if (addressinfo == nullptr) {
		ip[0] = '\0';
		freeaddrinfo(originalAddressInfo);
		return;
	}

	struct sockaddr_in *sockaddr_ipv4 = (struct sockaddr_in *) addressinfo->ai_addr;
	inet_ntop(AF_INET, &sockaddr_ipv4->sin_addr, ip, 65);
	freeaddrinfo(originalAddressInfo);
}

void DomainNameToIP_Berkley( const char *domainName, char ip[65] )
{
#if RAKNET_SUPPORT_IPV6==1
	return DomainNameToIP_Berkley_IPV4And6(domainName, ip);
#else
	return DomainNameToIP_Berkley_IPV4(domainName, ip);
#endif
}

void RNS2_Berkley::SetSocketOptions(void)
{
	int r;
	// This doubles the max throughput rate
	int sock_opt=1024*256;
	r = setsockopt__( rns2Socket, SOL_SOCKET, SO_RCVBUF, ( char * ) & sock_opt, sizeof ( sock_opt ) );
	RakAssert(r==0);

	// Immediate hard close. Don't linger the socket, or recreating the socket quickly on Vista fails.
	// Fail with voice and xbox

	sock_opt=0;
	r = setsockopt__( rns2Socket, SOL_SOCKET, SO_LINGER, ( char * ) & sock_opt, sizeof ( sock_opt ) );
	// Do not assert, ignore failure

#ifdef _WIN32
	// Disable SIO_UDP_CONNRESET behavior on Windows
	// By default, if a UDP sendto() results in an ICMP "port unreachable" response,
	// Windows will cause subsequent recvfrom() calls to fail with WSAECONNRESET (10054).
	// This is undesirable for UDP applications that may send to unreachable hosts.
	#ifndef SIO_UDP_CONNRESET
	#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
	#endif
	BOOL bNewBehavior = FALSE;
	DWORD dwBytesReturned = 0;
	WSAIoctl(rns2Socket, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior), nullptr, 0, &dwBytesReturned, nullptr, nullptr);
	// Ignore errors - this call may fail on older Windows versions
#endif

	// This doesn't make much difference: 10% maybe
	// Not supported on console 2
	sock_opt=1024*16;
	r = setsockopt__( rns2Socket, SOL_SOCKET, SO_SNDBUF, ( char * ) & sock_opt, sizeof ( sock_opt ) );
	RakAssert(r==0);

}

void RNS2_Berkley::SetNonBlockingSocket(unsigned long nonblocking)
{
#ifdef _WIN32
	SLNET_VERIFY( ioctlsocket__( rns2Socket, FIONBIO, &nonblocking ) == 0);
#else
	if (nonblocking)
		fcntl( rns2Socket, F_SETFL, O_NONBLOCK );
#endif
}
void RNS2_Berkley::SetBroadcastSocket(int broadcast)
{
	setsockopt__( rns2Socket, SOL_SOCKET, SO_BROADCAST, ( char * ) & broadcast, sizeof( broadcast ) );
}
void RNS2_Berkley::SetIPHdrIncl(int ipHdrIncl)
{

		setsockopt__( rns2Socket, IPPROTO_IP, IP_HDRINCL, ( char * ) & ipHdrIncl, sizeof( ipHdrIncl ) );

}
void RNS2_Berkley::SetDoNotFragment( int opt )
{
	#if defined( IP_DONTFRAGMENT )
 #if defined(_WIN32) && !defined(_DEBUG)
		// If this assert hit you improperly linked against WSock32.h
		RakAssert(IP_DONTFRAGMENT==14);
	#endif
		setsockopt__( rns2Socket, boundAddress.GetIPPROTO(), IP_DONTFRAGMENT, ( char * ) & opt, sizeof ( opt ) );
	#endif
}

void RNS2_Berkley::GetSystemAddressIPV4 ( RNS2Socket rns2Socket, SystemAddress *systemAddressOut )
{
	sockaddr_in sa;
	memset(&sa,0,sizeof(sockaddr_in));
	socklen_t len = sizeof(sa);
	//int r = 
		getsockname__(rns2Socket, (sockaddr*)&sa, &len);
	systemAddressOut->SetPortNetworkOrder(sa.sin_port);
	systemAddressOut->address.addr4.sin_addr.s_addr=sa.sin_addr.s_addr;

	if (systemAddressOut->address.addr4.sin_addr.s_addr == INADDR_ANY)
	{




		inet_pton(AF_INET, "127.0.0.1", &systemAddressOut->address.addr4.sin_addr.s_addr);

	}
}
void RNS2_Berkley::GetSystemAddressIPV4And6 ( RNS2Socket rns2Socket, SystemAddress *systemAddressOut )
{
#if RAKNET_SUPPORT_IPV6==1

	socklen_t slen;
	sockaddr_storage ss;
	slen = sizeof(ss);

	if ( getsockname__(rns2Socket, (struct sockaddr *)&ss, &slen)!=0)
	{
#if defined(_WIN32) && defined(_DEBUG)
		DWORD dwIOError = GetLastError();
		LPVOID messageBuffer;
		FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, dwIOError, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),  // Default language
			( LPTSTR ) & messageBuffer, 0, nullptr);
		// something has gone wrong here...
		RAKNET_DEBUG_TPRINTF( _T("getsockname failed:Error code - %d\n%s"), dwIOError, static_cast<LPTSTR>(messageBuffer));

		//Free the buffer.
		LocalFree( messageBuffer );
#endif
		systemAddressOut->FromString(0);
		return;
	}

	if (ss.ss_family==AF_INET)
	{
		memcpy(&systemAddressOut->address.addr4,(sockaddr_in *)&ss,sizeof(sockaddr_in));
		systemAddressOut->debugPort=ntohs(systemAddressOut->address.addr4.sin_port);

		uint32_t zero = 0;		
		if (memcmp(&systemAddressOut->address.addr4.sin_addr.s_addr, &zero, sizeof(zero))==0)
			systemAddressOut->SetToLoopback(4);
		//	systemAddressOut->address.addr4.sin_port=ntohs(systemAddressOut->address.addr4.sin_port);
	}
	else
	{
		memcpy(&systemAddressOut->address.addr6,(sockaddr_in6 *)&ss,sizeof(sockaddr_in6));
		systemAddressOut->debugPort=ntohs(systemAddressOut->address.addr6.sin6_port);

		char zero[16];
		memset(zero,0,sizeof(zero));
		if (memcmp(&systemAddressOut->address.addr4.sin_addr.s_addr, &zero, sizeof(zero))==0)
			systemAddressOut->SetToLoopback(6);

		//	systemAddressOut->address.addr6.sin6_port=ntohs(systemAddressOut->address.addr6.sin6_port);
	}

#else
	(void) rns2Socket;
	(void) systemAddressOut;
	return;
#endif
}

RNS2BindResult RNS2_Berkley::BindSharedIPV4( RNS2_BerkleyBindParameters *bindParameters, const char *file, unsigned int line ) {

	(void) file;
	(void) line;

	int ret;
	memset(&boundAddress.address.addr4,0,sizeof(sockaddr_in));
	boundAddress.address.addr4.sin_port = htons( bindParameters->port );
	rns2Socket = (int) socket__( bindParameters->addressFamily, bindParameters->type, bindParameters->protocol );
	if (rns2Socket == -1)
		return BR_FAILED_TO_BIND_SOCKET;

	SetSocketOptions();
	SetNonBlockingSocket(bindParameters->nonBlockingSocket);
	SetBroadcastSocket(bindParameters->setBroadcast);
	SetIPHdrIncl(bindParameters->setIPHdrIncl);

	// Fill in the rest of the address structure
	boundAddress.address.addr4.sin_family = AF_INET;

	if (bindParameters->hostAddress && bindParameters->hostAddress[0])
	{
		inet_pton(AF_INET, bindParameters->hostAddress, &boundAddress.address.addr4.sin_addr.s_addr);
	}
	else
	{
		//		RAKNET_DEBUG_PRINTF("Binding any on port %i\n", port);
		boundAddress.address.addr4.sin_addr.s_addr = INADDR_ANY;
	}

	// bind our name to the socket
	ret = bind__( rns2Socket, ( struct sockaddr * ) &boundAddress.address.addr4, sizeof( boundAddress.address.addr4 ) );

	if ( ret <= -1 )
	{
#if defined(_WIN32)
		closesocket__(rns2Socket);
		return BR_FAILED_TO_BIND_SOCKET;
#elif (defined(__GNUC__) || defined(__GCCXML__) ) && !defined(_WIN32)
		closesocket__(rns2Socket);
		switch (errno)
		{
		case EBADF:
			RAKNET_DEBUG_PRINTF("bind__(): sockfd is not a valid descriptor.\n"); break;
		case ENOTSOCK:
			RAKNET_DEBUG_PRINTF("bind__(): Argument is a descriptor for a file, not a socket.\n"); break;
		case EINVAL:
			RAKNET_DEBUG_PRINTF("bind__(): The addrlen is wrong, or the socket was not in the AF_UNIX family.\n"); break;
		case EROFS:
			RAKNET_DEBUG_PRINTF("bind__(): The socket inode would reside on a read-only file system.\n"); break;
		case EFAULT:
			RAKNET_DEBUG_PRINTF("bind__(): my_addr points outside the user's accessible address space.\n"); break;
		case ENAMETOOLONG:
			RAKNET_DEBUG_PRINTF("bind__(): my_addr is too long.\n"); break;
		case ENOENT:
			RAKNET_DEBUG_PRINTF("bind__(): The file does not exist.\n"); break;
		case ENOMEM:
			RAKNET_DEBUG_PRINTF("bind__(): Insufficient kernel memory was available.\n"); break;
		case ENOTDIR:
			RAKNET_DEBUG_PRINTF("bind__(): A component of the path prefix is not a directory.\n"); break;
		case EACCES:
			// Port reserved on PS4
			RAKNET_DEBUG_PRINTF("bind__(): Search permission is denied on a component of the path prefix.\n"); break;
		case ELOOP:
			RAKNET_DEBUG_PRINTF("bind__(): Too many symbolic links were encountered in resolving my_addr.\n"); break;
		default:
			RAKNET_DEBUG_PRINTF("Unknown bind__() error %i.\n", errno); break;
		}

		return BR_FAILED_TO_BIND_SOCKET;
#endif
	}

	GetSystemAddressIPV4(rns2Socket, &boundAddress );
	return BR_SUCCESS;
}

void PrepareAddrInfoHints2(addrinfo *hints)
{
	memset(hints, 0, sizeof(addrinfo)); // make sure the struct is empty
	hints->ai_socktype = SOCK_DGRAM; // UDP sockets
	hints->ai_flags = AI_PASSIVE; // fill in my IP for me
}

RNS2BindResult RNS2_Berkley::BindSharedIPV4And6( RNS2_BerkleyBindParameters *bindParameters, const char *file, unsigned int line ) {
	
	(void) file;
	(void) line;
	(void) bindParameters;

#if RAKNET_SUPPORT_IPV6==1

	int ret=0;
	struct addrinfo hints;
	struct addrinfo *servinfo=0, *aip;  // will point to the results
	PrepareAddrInfoHints2(&hints);
	hints.ai_family=bindParameters->addressFamily;
	char portStr[32];
	Itoa(bindParameters->port,portStr,10);


	// On Ubuntu, "" returns "No address associated with hostname" while 0 works.
	if (bindParameters->hostAddress && 
		(_stricmp(bindParameters->hostAddress,"UNASSIGNED_SYSTEM_ADDRESS")==0 || bindParameters->hostAddress[0]==0))
	{
		getaddrinfo(0, portStr, &hints, &servinfo);
	}
	else
	{
		getaddrinfo(bindParameters->hostAddress, portStr, &hints, &servinfo);
	}

	// Try all returned addresses until one works
	for (aip = servinfo; aip != nullptr; aip = aip->ai_next)
	{
		// Open socket. The address type depends on what
		// getaddrinfo() gave us.
		rns2Socket = socket__(aip->ai_family, aip->ai_socktype, aip->ai_protocol);

		if (rns2Socket == -1)
			return BR_FAILED_TO_BIND_SOCKET;

		// For IPv6 sockets, set IPV6_V6ONLY to allow binding both IPv4 and IPv6
		// sockets to the same port. Without this, an IPv6 socket on Linux/Windows
		// defaults to dual-stack mode (listening on both IPv4 and IPv6), which
		// prevents a separate IPv4 socket from binding to the same port.
		if (aip->ai_family == AF_INET6)
		{
			int ipv6only = 1;
			setsockopt__(rns2Socket, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&ipv6only, sizeof(ipv6only));
			// Ignore errors - some platforms may not support this option
		}

		ret = bind__(rns2Socket, aip->ai_addr, (int) aip->ai_addrlen );
		if (ret>=0)
		{
			if (aip->ai_family == AF_INET)
			{
				memcpy(&boundAddress.address.addr4, aip->ai_addr, sizeof(sockaddr_in));
			}
			else
			{
				memcpy(&boundAddress.address.addr6, aip->ai_addr, sizeof(sockaddr_in6));
			}

			freeaddrinfo(servinfo); // free the linked-list

			SetSocketOptions();
			SetNonBlockingSocket(bindParameters->nonBlockingSocket);
			SetBroadcastSocket(bindParameters->setBroadcast);
			SetIPHdrIncl(bindParameters->setIPHdrIncl);

			GetSystemAddressIPV4And6( rns2Socket, &boundAddress );
			
			return BR_SUCCESS;
		}
		else
		{
			closesocket__(rns2Socket);
		}
	}
	
	return BR_FAILED_TO_BIND_SOCKET;

#else
return BR_REQUIRES_RAKNET_SUPPORT_IPV6_DEFINE;
#endif
}

void RNS2_Berkley::RecvFromBlockingIPV4And6(RNS2RecvStruct *recvFromStruct)
{
#if RAKNET_SUPPORT_IPV6==1

	sockaddr_storage their_addr;
	sockaddr* sockAddrPtr;
	socklen_t sockLen;
	socklen_t* socketlenPtr=(socklen_t*) &sockLen;
	memset(&their_addr,0,sizeof(their_addr));
	int dataOutSize;
	const int flag=0;

	{
		sockLen=sizeof(their_addr);
		sockAddrPtr=(sockaddr*) &their_addr;
	}

	dataOutSize=MAXIMUM_MTU_SIZE;

	recvFromStruct->bytesRead = recvfrom__(rns2Socket, recvFromStruct->data, dataOutSize, flag, sockAddrPtr, socketlenPtr );

#if defined(_WIN32) && defined(_DEBUG)
	if (recvFromStruct->bytesRead==-1)
	{
		DWORD dwIOError = GetLastError();
		// 10035 = WSAEWOULDBLOCK (expected for non-blocking sockets)
		// 10054 = WSAECONNRESET (expected for UDP - prior sendto received ICMP port unreachable)
		if (dwIOError != 10035 && dwIOError != 10054)
		{
			LPVOID messageBuffer;
			FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				nullptr, dwIOError, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),  // Default language
				( LPTSTR ) & messageBuffer, 0, nullptr);
			// I see this hit on XP with IPV6 for some reason
			RAKNET_DEBUG_TPRINTF( _T("Warning: recvfrom failed:Error code - %d\n%s"), dwIOError, static_cast<LPTSTR>(messageBuffer) );
			LocalFree( messageBuffer );
		}
	}	
#endif

	if (recvFromStruct->bytesRead<=0)
		return;
	recvFromStruct->timeRead= MafiaNet::GetTimeUS();

	{
		if (their_addr.ss_family==AF_INET)
		{
			memcpy(&recvFromStruct->systemAddress.address.addr4,(sockaddr_in *)&their_addr,sizeof(sockaddr_in));
			recvFromStruct->systemAddress.debugPort=ntohs(recvFromStruct->systemAddress.address.addr4.sin_port);
			//	systemAddressOut->address.addr4.sin_port=ntohs( systemAddressOut->address.addr4.sin_port );
		}
		else
		{
			memcpy(&recvFromStruct->systemAddress.address.addr6,(sockaddr_in6 *)&their_addr,sizeof(sockaddr_in6));
			recvFromStruct->systemAddress.debugPort=ntohs(recvFromStruct->systemAddress.address.addr6.sin6_port);
			//	systemAddressOut->address.addr6.sin6_port=ntohs( systemAddressOut->address.addr6.sin6_port );
		}
	}

#else
	(void) recvFromStruct;
#endif
}

void RNS2_Berkley::RecvFromBlockingIPV4(RNS2RecvStruct *recvFromStruct)
{
	sockaddr* sockAddrPtr;
	socklen_t sockLen;
	socklen_t* socketlenPtr=(socklen_t*) &sockLen;
	sockaddr_in sa;
	memset(&sa,0,sizeof(sockaddr_in));
	const int flag=0;
	
	{
		sockLen=sizeof(sa);
		sa.sin_family = AF_INET;
		sa.sin_port=0;
		sockAddrPtr=(sockaddr*) &sa;
	}

	recvFromStruct->bytesRead = recvfrom__( GetSocket(), recvFromStruct->data, sizeof(recvFromStruct->data), flag, sockAddrPtr, socketlenPtr );

	if (recvFromStruct->bytesRead<=0)
	{
		return;
	}
	recvFromStruct->timeRead= MafiaNet::GetTimeUS();

	{
		recvFromStruct->systemAddress.SetPortNetworkOrder( sa.sin_port );
		recvFromStruct->systemAddress.address.addr4.sin_addr.s_addr=sa.sin_addr.s_addr;
	}
}

void RNS2_Berkley::RecvFromBlocking(RNS2RecvStruct *recvFromStruct)
{
#if RAKNET_SUPPORT_IPV6==1
	return RecvFromBlockingIPV4And6(recvFromStruct);
#else
	return RecvFromBlockingIPV4(recvFromStruct);
#endif
}

#endif // file header

#endif // #ifdef RAKNET_SOCKET_2_INLINE_FUNCTIONS
