/**
 * @file krlvintegrity.h
 * @brief KRLV state-file integrity: keyed signatures (HMAC-SHA256 via a hook the
 * viewer provides), a per-install key, and atomic writes so a crash mid-write
 * is never mistaken for tampering.
 *
 * KRLV does not link a crypto library itself. The viewer registers the HMAC
 * function at startup (setHmacSha256Hook), the same pattern as the IM-send hook.
 * This is integrity only (an authentication code), not encryption: nothing is
 * hidden, and the function only proves a file was not changed.
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

#ifndef KRLV_KRLVINTEGRITY_H
#define KRLV_KRLVINTEGRITY_H

#include <functional>
#include <string>

#include "llsd.h"

namespace KRlv
{
    // HMAC-SHA256(key, data) as lowercase hex. Provided by the viewer.
    using HmacSha256Fn = std::function<std::string(const std::string& key, const std::string& data)>;

    // Called once at viewer startup. Without it nothing can be signed or verified,
    // so state files are neither saved nor loaded.
    void setHmacSha256Hook(HmacSha256Fn hook);
    bool hasHmacSha256Hook();

    // Returns the per-install key, creating and storing it in gSavedSettings if it
    // does not exist yet. Call after login so the key is saved with settings.
    std::string ensureIntegrityKey();

    // Keyed signature over a canonical serialisation of `payload` (the signature
    // field itself must be removed first). Uses the per-install key. Returns an
    // empty string if no hook is registered.
    std::string keyedSignature(const LLSD& payload, const std::string& domain);

    // Signs `root` in place: sets sigscheme to hmac-sha256 and the signature over the
    // document as it stands. Returns false (nothing signed) when no hook is registered.
    bool signDocument(LLSD& root, const std::string& domain);

    // Verifies a loaded document. Any sigscheme other than hmac-sha256 counts as
    // tampered, as does a missing or mismatching signature.
    bool verifyDocument(const LLSD& root, const std::string& domain);

    // Writes `root` to `path` via path.tmp, then renames it over `path`. The rename
    // is the atomic step; the data is flushed but NOT fsynced, so a power loss can
    // still leave a zero-length or partial file, which the loaders treat as corrupt.
    bool writeAtomically(const std::string& path, const LLSD& root);

    // Reads `path`. If it is missing, returns false with `status` "missing". If the
    // file exists but is unreadable or not valid LLSD, status "corrupt". Otherwise
    // `root` holds the parsed document and status is "ok".
    bool readDocument(const std::string& path, LLSD& root, std::string& status);
}

#endif // KRLV_KRLVINTEGRITY_H
