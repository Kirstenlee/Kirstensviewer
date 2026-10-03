/**
 * @file krlvchat.h
 * @brief KRLV Chat category's newview-facing query functions. Unlike
 * krlvbehaviours.h's registration functions (krlv-internal, called only
 * from krlvhandler.cpp's init()), these are meant to be called FROM
 * newview code at the exact points that need to apply a chat-volume or
 * channel restriction - see krlv/README.md's Chat section for why these
 * exist as purpose-built queries rather than a generic KRlvHandler
 * getter (keeps RLVa-specific reduction/matching logic inside krlv/
 * rather than leaking into newview code).
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
 *
 * KRLV is a clean-room implementation, written entirely from the public
 * RLVa API specification (the command names, syntax and behaviour
 * described at
 * https://wiki.secondlife.com/wiki/LSL_Protocol/RestrainedLoveAPI), with
 * respect and thanks to Marine Kelley, who created the original
 * RestrainedLove API and viewer, and Kitty Barnett, who created RLVa
 * (Restrained Love Viewer - Advanced). No code from their viewers, or from
 * any RLV/RLVa source tree, was read, copied, or referenced in writing this
 * module - only the published API document.
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

#ifndef KRLV_KRLVCHAT_H
#define KRLV_KRLVCHAT_H

#include <string>
#include <vector>

#include "llerror.h" // stdtypes.h anchor, for S32
#include "llchat.h"  // EChatType

namespace KRlv
{
    // @chatshout/@chatnormal/@chatwhisper - clamps `original` down (or,
    // for @chatwhisper specifically, back up to normal) to the effective
    // allowed volume. Treats @chatshout/@chatnormal as a CEILING and
    // @chatwhisper as a FLOOR on an ordinal whisper<normal<shout scale;
    // if both ever produce a contradictory floor/ceiling (not addressed
    // by the spec), the floor wins - a documented engineering choice,
    // not an inferred RLVa rule. Only meaningful for
    // CHAT_TYPE_WHISPER/NORMAL/SHOUT - pass other types through
    // unchanged.
    EChatType getClampedChatType(EChatType original);

    // @sendchannel_except:<channel> - true if this specific channel is
    // in the blocked list (independent of whether the broader
    // @sendchannel restriction is active at all - see krlv/README.md).
    bool isChannelSendBlocked(S32 channel);

    // @redirchat (isEmote=false) / @rediremote (isEmote=true) - true if
    // THIS message type is currently being redirected away from the
    // public channel, filling `outChannels` with every configured
    // destination ("if several redirections are issued, the message
    // will be redirected to each channel" - spec's own wording).
    // Independent of @sendchat's own restriction state - see
    // krlv/README.md and krlvchat.cpp's file header.
    bool getChatRedirectChannels(bool isEmote, std::vector<S32>& outChannels);

    // @sendchat's full spec-described filtering rule for a message the
    // caller has ALREADY determined starts with '/' (plain,
    // non-slash-prefixed text has nothing to filter - the caller
    // discards it outright before ever reaching this). Returns false if
    // the message must be discarded entirely (a special character was
    // present); true if it may proceed, with `text` possibly truncated
    // in place - at a literal period regardless, then to 30/15
    // characters (emote/other) unless @emote's exception is active. See
    // krlvchat.cpp's file header for the exact rule.
    bool filterSendChatText(std::string& text, bool isEmote);
}

#endif // KRLV_KRLVCHAT_H
