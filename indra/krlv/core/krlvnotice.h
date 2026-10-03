/**
 * @file krlvnotice.h
 * @brief KRLV owner notices with flood protection. Every automatic owner IM goes
 * through sendOwnerNotice(): a per-kind minimum interval stops a looping script or
 * a fast-moving event from flooding the owners, and the held-back repeats are
 * summarised in the next notice that does go out.
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

#ifndef KRLV_KRLVNOTICE_H
#define KRLV_KRLVNOTICE_H

#include <string>

namespace KRlv
{
    // The kinds of owner notice. Each maps to one per-owner choice in krlvowners.h,
    // except Safeword, which always reaches every owner.
    enum class KRlvNoticeKind
    {
        Settings,       // KRLV or protected settings accessed or changed
        Tamper,         // a state file corrupted or deleted
        Timestamp,      // timestamp mismatch between state files
        OwnerChange,    // owner added, removed or changed
        Safeword        // wearer used the safeword - never rate-limited, all owners
    };

    // Sends `text` as an IM to every owner who chose this kind of notice, subject to
    // the flood limit. Returns false when the notice was held back (rate limit) and
    // true when it was sent or there was nobody to send it to. Safeword notices are
    // never held back.
    bool sendOwnerNotice(KRlvNoticeKind kind, const std::string& text);
}

#endif // KRLV_KRLVNOTICE_H
