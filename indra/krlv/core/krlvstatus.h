/**
 * @file krlvstatus.h
 * @brief KRLV status facts for the Control tab's about box and status panel:
 * version numbers and whether tamper protection is running.
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

#ifndef KRLV_KRLVSTATUS_H
#define KRLV_KRLVSTATUS_H

#include <string>

namespace KRlv
{
    // KRLV's own version, e.g. "1.0.0".
    std::string krlvVersionText();

    // The RLV API version this implementation targets: the latest version listed in the
    // KRLV tracker, e.g. "2.9.29".
    std::string rlvApiVersionText();

    // True once the tamper checks have been started for this login.
    bool tamperChecksActive();

    // When the owner state was last looked at, as text, or "not yet checked".
    std::string tamperLastCheckText();
}

#endif // KRLV_KRLVSTATUS_H
