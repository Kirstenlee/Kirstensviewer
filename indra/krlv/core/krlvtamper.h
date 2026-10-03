/**
 * @file krlvtamper.h
 * @brief KRLV tamper detection with hysteresis. Observes the state files after login,
 * and reports an incident to the owners only when the same problem is seen in two
 * observations 60 seconds apart, and only once per incident.
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

#ifndef KRLV_KRLVTAMPER_H
#define KRLV_KRLVTAMPER_H

#include <string>
#include <vector>

namespace KRlv
{
    // Starts the checks for this login: a first observation shortly after login, and a
    // confirmation observation 60 seconds later. Call once, when the account is ready.
    void startTamperChecks();
}

#endif // KRLV_KRLVTAMPER_H
