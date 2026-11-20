/**
 * @file lludpreceiverthread.cpp
 * @brief Background thread that pulls raw UDP packets off the socket
 *        and hands them to the main thread via a thread-safe queue.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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
#include "lludpreceiverthread.h"
#include "llproxy.h"
#include "message.h"
#include "net.h"

#include <boost/fiber/algo/round_robin.hpp>

LLUDPReceiverThread::LLUDPReceiverThread(S32 hSocket)
:   LLThread("UDP Receiver"),
    mSocket(hSocket)
{
}

LLUDPReceiverThread::~LLUDPReceiverThread()
{
}

void LLUDPReceiverThread::run()
{
#if LL_WINDOWS
    // LLThreadSafeQueue is built on boost::fibers primitives, which require
    // a fiber scheduling algorithm to be installed on the thread.
    // Todo: most threads do not need coroutines, perhaps find a way around?
    boost::fibers::use_scheduling_algorithm<boost::fibers::algo::round_robin>();
#endif // LL_WINDOWS

    // Reads one datagram from the socket, returns its size (0 if none)
    auto read_one = [this](LLPacketBuffer& pkt) -> S32
    {
        if (LLProxy::isSOCKSProxyEnabled())
        {
            char buffer[NET_BUFFER_SIZE + SOCKS_HEADER_SIZE];   /* Flawfinder ignore */
            S32 size = receive_packet(mSocket, buffer);
            if (size > SOCKS_HEADER_SIZE)
            {
                proxywrap_t* header = static_cast<proxywrap_t*>(static_cast<void*>(buffer));
                LLHost sender;
                sender.setAddress(header->addr);
                sender.setPort(ntohs(header->port));
                size -= SOCKS_HEADER_SIZE;
                pkt.init(buffer + SOCKS_HEADER_SIZE, size, sender);
                return size;
            }
            return 0;
        }
        pkt.init(mSocket);
        return pkt.getSize();
    };

    // S24 : the socket is always drained first, then a bounded batch is decoded and
    // the socket is checked again.  Decoding a long backlog without reading would let
    // the kernel buffer overflow and drop datagrams silently.
    constexpr S32 MAX_READS_PER_PASS = 4096;
    constexpr S32 MAX_DECODES_PER_PASS = 256;
    while (!isQuitting())
    {
        S32 read_count = 0;
        while (read_count < MAX_READS_PER_PASS)
        {
            LLHost invalid_host;
            LLPacketBuffer pkt(invalid_host, nullptr, 0);
            if (read_one(pkt) <= 0)
            {
                break;
            }
            gMessageSystem->processBufferPacket(pkt);
            ++read_count;
        }

        // Decode a batch from the priority rings and hand each result to the
        // main thread's dispatch queue.  decodeDataOwned() returns nullptr both
        // when the rings are empty and when a packet was invalid/banned/off-circuit
        // and should simply be dropped.
        S32 decoded_count = 0;
        while (decoded_count < MAX_DECODES_PER_PASS
               && gMessageSystem->getNumBufferedPackets() > 0
               && !gMessageSystem->isDecodedQueueFull())
        {
            ++decoded_count;
            std::unique_ptr<LLDecodedMessage> decoded = gMessageSystem->decodeDataOwned();
            if (decoded)
            {
                // Route and handler are snapshotted together under a
                // single lock, so a registration change can't cause a
                // main-thread-only handler to run here; in that case
                // this returns false without calling anything and the
                // message goes to the main thread's queue.
                if (!gMessageSystem->tryDispatchDecodedOnThread(*decoded))
                {
                    const LLHost sender = decoded->mSender;
                    const TPACKETID packet_id = decoded->mPacketID;
                    const bool reliable = decoded->mReliable;
                    if (gMessageSystem->pushDecoded(decoded))
                    {
                        gMessageSystem->markDecodedReceived(sender, packet_id, reliable);
                    }
                }
                else
                {
                    gMessageSystem->markDecodedReceived(decoded->mSender, decoded->mPacketID, decoded->mReliable);
                    gMessageSystem->ackDecoded(*decoded);
                }
            }
        }

        if (read_count == 0 && decoded_count == 0)
        {
            // nothing to read or decode: wait on the socket so an arriving packet
            // wakes the thread at once
            wait_for_packet(mSocket, 5);
        }
    }
}
