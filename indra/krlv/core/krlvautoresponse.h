/**
 * @file krlvautoresponse.h
 * @brief KRLV auto-response for dropped IM and chat. When a restriction (@recvim or
 * @recvchat) drops an incoming message, the sender receives one short IM saying so,
 * modelled on the viewer's Do Not Disturb response. One reply per sender per cooldown,
 * so it can never become a loop. The text is editable in KRLV Control.
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
 *
 * KRLV is a clean-room implementation, written entirely from the public
 * RLVa API specification, with respect and thanks to Marine Kelley and Kitty
 * Barnett. No code from their viewers was read, copied or referenced.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef KRLV_KRLVAUTORESPONSE_H
#define KRLV_KRLVAUTORESPONSE_H

#include "lluuid.h"

namespace KRlv
{
    // Called where an inbound IM is dropped by @recvim.
    void autoRespondDroppedIM(const LLUUID& fromId);

    // Called where an inbound chat line is dropped by @recvchat. Objects are ignored:
    // only avatars receive a reply.
    void autoRespondDroppedChat(const LLUUID& fromId);
}

#endif // KRLV_KRLVAUTORESPONSE_H
