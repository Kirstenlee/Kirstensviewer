/**
 * @file krlvsafeword.h
 * @brief KRLV safeword - the wearer's three-word panic phrase. When the wearer
 * types it, in order, in any chat or IM, KRLV clears every restriction, turns
 * KRLV off and tells all owners. The message itself still goes out.
 *
 * The phrase is stored salted and hashed, never in plain text. Matching works on
 * each run of three consecutive words in the outgoing message, so the phrase is
 * never needed in readable form.
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

#ifndef KRLV_KRLVSAFEWORD_H
#define KRLV_KRLVSAFEWORD_H

#include <string>

namespace KRlv
{
    // True when a safeword has been set.
    bool hasSafeword();

    // Validates and stores the phrase. It must be exactly three words separated by
    // single spaces, each word letters and digits only. Matching ignores case.
    // On failure returns false and sets `error` to a short reason.
    bool setSafeword(const std::string& phrase, std::string& error);

    // Checks an outgoing chat or IM message. If the three-word phrase appears in it
    // in order, runs the safeword response and returns true. The caller must still
    // send the message: the safeword never blocks the wearer's own message.
    bool checkOutgoingSafeword(const std::string& text);
}

#endif // KRLV_KRLVSAFEWORD_H
