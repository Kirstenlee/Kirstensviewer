/**
 * @file net.cpp
 * @brief Cross-platform routines for sending and receiving packets.
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

//#include "net.h"

// system library includes
#include <stdexcept>

#if LL_WINDOWS
#include "llwin32headers.h"
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

// linden library includes
#include "llerror.h"
#include "llhost.h"
#include "lltimer.h"
#include "indra_constants.h"

// Globals
#if LL_WINDOWS

SOCKADDR_IN stDstAddr;
SOCKADDR_IN stSrcAddr;
SOCKADDR_IN stLclAddr;
static WSADATA stWSAData;

#else

struct sockaddr_in stDstAddr;
struct sockaddr_in stSrcAddr;
struct sockaddr_in stLclAddr;

#if LL_DARWIN
#ifndef _SOCKLEN_T
#define _SOCKLEN_T
typedef int socklen_t;
#endif
#endif

#endif

static U32 gsnReceivingIFAddr = INVALID_HOST_IP_ADDRESS; // Address to which datagram was sent

const char* LOOPBACK_ADDRESS_STRING = "127.0.0.1";
const char* BROADCAST_ADDRESS_STRING = "255.255.255.255";

const int   SEND_BUFFER_SIZE    = 200000;
const int   RECEIVE_BUFFER_SIZE = 800000;

// universal functions (cross-platform)

LLHost get_sender()
{
    return LLHost(stSrcAddr.sin_addr.s_addr, ntohs(stSrcAddr.sin_port));
}

U32 get_sender_ip(void)
{
    return stSrcAddr.sin_addr.s_addr;
}

U32 get_sender_port()
{
    return ntohs(stSrcAddr.sin_port);
}

LLHost get_receiving_interface()
{
    return LLHost(gsnReceivingIFAddr, INVALID_PORT);
}

U32 get_receiving_interface_ip(void)
{
    return gsnReceivingIFAddr;
}

const char* u32_to_ip_string(U32 ip)
{
    static char buffer[MAXADDRSTR];  /* Flawfinder: ignore */

    // Convert the IP address into a string
    in_addr in;
    in.s_addr = ip;
    char* result = inet_ntoa(in);

    // NULL indicates error in conversion
    if (result != NULL)
    {
        strncpy( buffer, result, MAXADDRSTR );   /* Flawfinder: ignore */
        buffer[MAXADDRSTR-1] = '\0';
        return buffer;
    }
    else
    {
        return "(bad IP addr)";
    }
}


// Returns ip_string if successful, NULL if not.  Copies into ip_string
char *u32_to_ip_string(U32 ip, char *ip_string)
{
    char *result;
    in_addr in;

    // Convert the IP address into a string
    in.s_addr = ip;
    result = inet_ntoa(in);

    // NULL indicates error in conversion
    if (result != NULL)
    {
        //the function signature needs to change to pass in the lengfth of first and last.
        strcpy(ip_string, result);  /*Flawfinder: ignore*/
        return ip_string;
    }
    else
    {
        return NULL;
    }
}


// Wrapper for inet_addr()
U32 ip_string_to_u32(const char* ip_string)
{
    // *NOTE: Windows doesn't support inet_aton(), so we are using
    // inet_addr(). Unfortunately, INADDR_NONE == INADDR_BROADCAST, so
    // we have to check whether the input is a broadcast address before
    // deciding that @ip_string is invalid.
    //
    // Also, our definition of INVALID_HOST_IP_ADDRESS doesn't allow us to
    // use wildcard addresses. -Ambroff
    U32 ip = inet_addr(ip_string);
    if (ip == INADDR_NONE
            && strncmp(ip_string, BROADCAST_ADDRESS_STRING, MAXADDRSTR) != 0)
    {
        LL_WARNS() << "ip_string_to_u32() failed, Error: Invalid IP string '" << ip_string << "'" << LL_ENDL;
        return INVALID_HOST_IP_ADDRESS;
    }
    return ip;
}


S32 start_net(S32& socket_out, int& nPort)
{
    // Create socket, make non-blocking
    // Init WinSock
    int nRet;
    int hSocket;

    int snd_size = SEND_BUFFER_SIZE;
    int rec_size = RECEIVE_BUFFER_SIZE;
    int buff_size = 4;

    // Initialize windows specific stuff
    if (WSAStartup(0x0202, &stWSAData))
    {
        S32 err = WSAGetLastError();
        WSACleanup();
        LL_WARNS("AppInit") << "Windows Sockets initialization failed, err " << err << LL_ENDL;
        return 1;
    }

    // Get a datagram socket
    hSocket = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (hSocket == INVALID_SOCKET)
    {
        S32 err = WSAGetLastError();
        WSACleanup();
        LL_WARNS("AppInit") << "socket() failed, err " << err << LL_ENDL;
        return 2;
    }

    // Name the socket (assign the local port number to receive on)
    stLclAddr.sin_family      = AF_INET;
    stLclAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    stLclAddr.sin_port        = htons(nPort);

    S32 attempt_port = nPort;
    LL_DEBUGS("AppInit") << "attempting to connect on port " << attempt_port << LL_ENDL;
    nRet = bind(hSocket, (struct sockaddr*) &stLclAddr, sizeof(stLclAddr));

    if (nRet == SOCKET_ERROR)
    {
        // If we got an address in use error...
        if (WSAGetLastError() == WSAEADDRINUSE)
        {
            // Try all ports from PORT_DISCOVERY_RANGE_MIN to PORT_DISCOVERY_RANGE_MAX
            for(attempt_port = PORT_DISCOVERY_RANGE_MIN;
                attempt_port <= PORT_DISCOVERY_RANGE_MAX;
                attempt_port++)
            {
                stLclAddr.sin_port = htons(attempt_port);
                LL_DEBUGS("AppInit") << "trying port " << attempt_port << LL_ENDL;
                nRet = bind(hSocket, (struct sockaddr*) &stLclAddr, sizeof(stLclAddr));

                if (!(nRet == SOCKET_ERROR &&
                    WSAGetLastError() == WSAEADDRINUSE))
                {
                    break;
                }
            }

            if (nRet == SOCKET_ERROR)
            {
                LL_WARNS("AppInit") << "startNet() : Couldn't find available network port." << LL_ENDL;
                // Fail gracefully here in release
                return 3;
            }
        }
        else
        // Some other socket error
        {
            LL_WARNS("AppInit") << llformat("bind() port: %d failed, Err: %d\n", nPort, WSAGetLastError()) << LL_ENDL;
            // Fail gracefully in release.
            return 4;
        }
    }

    sockaddr_in socket_address;
    S32 socket_address_size = sizeof(socket_address);
    getsockname(hSocket, (SOCKADDR*) &socket_address, &socket_address_size);
    attempt_port = ntohs(socket_address.sin_port);

    LL_INFOS("AppInit") << "connected on port " << attempt_port << LL_ENDL;
    nPort = attempt_port;

    // Set socket to be non-blocking
    unsigned long argp = 1;
    nRet = ioctlsocket (hSocket, FIONBIO, &argp);
    if (nRet == SOCKET_ERROR)
    {
        printf("Failed to set socket non-blocking, Err: %d\n",
        WSAGetLastError());
    }

    // set a large receive buffer
    nRet = setsockopt(hSocket, SOL_SOCKET, SO_RCVBUF, (char*)&rec_size, buff_size);
    if (nRet)
    {
        LL_INFOS("AppInit") << "Can't set receive buffer size!" << LL_ENDL;
    }

    nRet = setsockopt(hSocket, SOL_SOCKET, SO_SNDBUF, (char*)&snd_size, buff_size);
    if (nRet)
    {
        LL_INFOS("AppInit") << "Can't set send buffer size!" << LL_ENDL;
    }

    getsockopt(hSocket, SOL_SOCKET, SO_RCVBUF, (char *)&rec_size, &buff_size);
    getsockopt(hSocket, SOL_SOCKET, SO_SNDBUF, (char *)&snd_size, &buff_size);

    LL_DEBUGS("AppInit") << "startNet - receive buffer size : " << rec_size << LL_ENDL;
    LL_DEBUGS("AppInit") << "startNet - send buffer size    : " << snd_size << LL_ENDL;

    //  Setup a destination address
    stDstAddr.sin_family =      AF_INET;
    stDstAddr.sin_addr.s_addr = INVALID_HOST_IP_ADDRESS;
    stDstAddr.sin_port =        htons(nPort);

    socket_out = hSocket;
    return 0;
}

void end_net(S32& socket_out)
{
    if (socket_out >= 0)
    {
        shutdown(socket_out, SD_BOTH);
        closesocket(socket_out);
    }
    WSACleanup();
}

S32 receive_packet(int hSocket, char * receiveBuffer)
{
    //  Receives data asynchronously from the socket set by initNet().
    //  Returns the number of bytes received into dataReceived, or zero
    //  if there is no data received.
    int nRet;
    int addr_size = sizeof(struct sockaddr_in);

    nRet = recvfrom(hSocket, receiveBuffer, NET_BUFFER_SIZE, 0, (struct sockaddr*)&stSrcAddr, &addr_size);
    if (nRet == SOCKET_ERROR )
    {
        if (WSAEWOULDBLOCK == WSAGetLastError())
            return 0;
        if (WSAECONNRESET == WSAGetLastError())
            return 0;
        LL_INFOS() << "receivePacket() failed, Error: " << WSAGetLastError() << LL_ENDL;
    }

    return nRet;
}

// Returns true on success.
bool send_packet(int hSocket, const char *sendBuffer, int size, U32 recipient, int nPort)
{
    //  Sends a packet to the address set in initNet
    //
    int nRet = 0;
    U32 last_error = 0;

    stDstAddr.sin_addr.s_addr = recipient;
    stDstAddr.sin_port = htons(nPort);
    do
    {
        nRet = sendto(hSocket, sendBuffer, size, 0, (struct sockaddr*)&stDstAddr, sizeof(stDstAddr));

        if (nRet == SOCKET_ERROR )
        {
            last_error = WSAGetLastError();
            if (last_error != WSAEWOULDBLOCK)
            {
                // WSAECONNRESET - I think this is caused by an ICMP "connection refused"
                
                if (WSAECONNRESET == WSAGetLastError())
                {
                    return true;
                }
                LL_INFOS() << "sendto() failed to " << u32_to_ip_string(recipient) << ":" << nPort
                    << ", Error " << last_error << LL_ENDL;
            }
        }
    } while (  (nRet == SOCKET_ERROR)
             &&(last_error == WSAEWOULDBLOCK));

    return (nRet != SOCKET_ERROR);
}
