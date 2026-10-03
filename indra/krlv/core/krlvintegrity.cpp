/**
 * @file krlvintegrity.cpp
 * @brief KRLV state-file integrity - see krlvintegrity.h.
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
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

#include "krlvintegrity.h"

#include <sstream>

#include "llerror.h"
#include "llcontrol.h"
#include "lldir.h"
#include "llfile.h"
#include "llsdserialize.h"
#include "lluuid.h"

// KRLV: gSavedSettings is the same extern every other krlv/core file relies on.
extern LLControlGroup gSavedSettings;

namespace
{
    KRlv::HmacSha256Fn sHmacHook;
}

namespace KRlv
{
    void setHmacSha256Hook(HmacSha256Fn hook)
    {
        sHmacHook = std::move(hook);
    }

    bool hasHmacSha256Hook()
    {
        return static_cast<bool>(sHmacHook);
    }

    std::string ensureIntegrityKey()
    {
        std::string key = gSavedSettings.getString("KRLVIntegrityKey");
        if (key.empty())
        {
            // 256 bits from two random UUIDs, hyphens removed.
            std::string hex;
            for (char c : LLUUID::generateNewID().asString() + LLUUID::generateNewID().asString())
            {
                if (c != '-')
                {
                    hex += c;
                }
            }
            key = hex;
            gSavedSettings.setString("KRLVIntegrityKey", key);
        }
        return key;
    }

    std::string keyedSignature(const LLSD& payload, const std::string& domain)
    {
        if (!sHmacHook)
        {
            return std::string();
        }
        std::ostringstream canonical;
        LLSDSerialize::toXML(payload, canonical);
        return sHmacHook(ensureIntegrityKey(), domain + "\n" + canonical.str());
    }

    bool signDocument(LLSD& root, const std::string& domain)
    {
        if (!hasHmacSha256Hook())
        {
            LL_WARNS("KRLV") << "KRLV state not saved (" << domain << "): no integrity hook is registered." << LL_ENDL;
            return false;
        }
        root["sigscheme"] = std::string("hmac-sha256");
        root["signature"] = keyedSignature(root, domain);
        return !root["signature"].asString().empty();
    }

    bool verifyDocument(const LLSD& root, const std::string& domain)
    {
        if (!root.has("sigscheme") || root["sigscheme"].asString() != "hmac-sha256")
        {
            return false;
        }
        LLSD unsignedRoot = root;
        unsignedRoot.erase("signature");
        const std::string expected = keyedSignature(unsignedRoot, domain);
        return !expected.empty() && root["signature"].asString() == expected;
    }

    bool writeAtomically(const std::string& path, const LLSD& root)
    {
        const std::string temp = path + ".tmp";

        {
            llofstream out(temp.c_str(), std::ios_base::out | std::ios_base::binary);
            if (!out.good())
            {
                LL_WARNS("KRLV") << "Unable to open " << temp << " for atomic write." << LL_ENDL;
                return false;
            }
            LLSDSerialize::toBinary(root, out);
            out.flush();
            if (!out.good())
            {
                out.close();
                LLFile::remove(temp);
                return false;
            }
        }

        // LLFile::rename uses MoveFileEx with MOVEFILE_REPLACE_EXISTING on Windows,
        // so an existing target is replaced by this single call.
        if (LLFile::rename(temp, path) != 0)
        {
            LLFile::remove(temp);
            return false;
        }
        return true;
    }

    bool readDocument(const std::string& path, LLSD& root, std::string& status)
    {
        if (!gDirUtilp->fileExists(path))
        {
            status = "missing";
            return false;
        }

        llifstream in(path.c_str(), std::ios_base::in | std::ios_base::binary);
        if (!in.is_open())
        {
            status = "corrupt";
            return false;
        }
        LLSD parsed;
        const S32 bytes = LLSDSerialize::fromBinary(parsed, in, LLSDSerialize::SIZE_UNLIMITED);
        in.close();

        if (bytes <= 0 || !parsed.isMap())
        {
            status = "corrupt";
            return false;
        }
        root = parsed;
        status = "ok";
        return true;
    }
}
