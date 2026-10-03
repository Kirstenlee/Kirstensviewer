/**
 * @file krlvsafeword.cpp
 * @brief KRLV safeword - see krlvsafeword.h.
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

#include "krlvsafeword.h"

#include <cctype>
#include <vector>

#include "llerror.h"
#include "llcontrol.h"
#include "lldate.h"
#include "llmd5.h"
#include "lluuid.h"
#include "krlvhandler.h"
#include "krlvnotice.h"

// KRLV: gSavedSettings is the same extern every other krlv/core file relies on.
extern LLControlGroup gSavedSettings;

namespace
{
    // Lowercases and splits on anything that is not an ASCII letter or digit, so
    // "Jar, BIG hive!" and "jar big hive" give the same words.
    std::vector<std::string> normalizedWords(const std::string& text)
    {
        std::vector<std::string> words;
        std::string current;
        for (unsigned char c : text)
        {
            if (std::isalnum(c))
            {
                current += static_cast<char>(std::tolower(c));
            }
            else if (!current.empty())
            {
                words.push_back(current);
                current.clear();
            }
        }
        if (!current.empty())
        {
            words.push_back(current);
        }
        return words;
    }

    std::string hashTriple(const std::string& salt, const std::string& canonical)
    {
        LLMD5 md5;
        md5.update(salt + ":" + canonical);
        md5.finalize();
        char hex[33];
        md5.hex_digest(hex);
        return std::string(hex);
    }

    void runSafewordResponse()
    {
        // Ceases all RLVa activity: clear every restriction, turn KRLV off, and keep the
        // Control floater's checkbox in step so it does not show KRLV as still on.
        KRlvHandler::instance().clearAllRestrictions();
        KRlvHandler::instance().setEnabled(false);
        gSavedSettings.setBOOL("KRLVControlEnabled", false);

        LL_DEBUGS("KRLV") << "Safeword used - all restrictions cleared and KRLV turned off." << LL_ENDL;

        KRlv::sendOwnerNotice(KRlv::KRlvNoticeKind::Safeword,
            "KRLV Control: the wearer used the safeword at " + LLDate::now().asString()
            + ". All restrictions were cleared and KRLV was turned off.");
    }
}

namespace KRlv
{
    bool hasSafeword()
    {
        return !gSavedSettings.getString("KRLVSafewordHash").empty();
    }

    bool setSafeword(const std::string& phrase, std::string& error)
    {
        // Exactly three words, separated by single spaces, letters and digits only.
        std::vector<std::string> parts;
        std::string current;
        for (char c : phrase)
        {
            if (c == ' ')
            {
                parts.push_back(current);
                current.clear();
            }
            else
            {
                current += c;
            }
        }
        parts.push_back(current);

        if (parts.size() != 3)
        {
            error = "The safeword must be exactly three words separated by single spaces.";
            return false;
        }
        for (const std::string& part : parts)
        {
            if (part.empty())
            {
                error = "The safeword must be exactly three words separated by single spaces.";
                return false;
            }
            for (unsigned char c : part)
            {
                if (!std::isalnum(c))
                {
                    error = "Safeword words may contain only letters and numbers.";
                    return false;
                }
            }
        }

        std::vector<std::string> words = normalizedWords(phrase);
        if (words.size() != 3)
        {
            error = "The safeword must be exactly three words.";
            return false;
        }
        const std::string canonical = words[0] + " " + words[1] + " " + words[2];

        const std::string salt = LLUUID::generateNewID().asString();
        gSavedSettings.setString("KRLVSafewordSalt", salt);
        gSavedSettings.setString("KRLVSafewordHash", hashTriple(salt, canonical));
        return true;
    }

    bool checkOutgoingSafeword(const std::string& text)
    {
        if (!hasSafeword())
        {
            return false;
        }

        const std::string salt = gSavedSettings.getString("KRLVSafewordSalt");
        const std::string stored = gSavedSettings.getString("KRLVSafewordHash");
        const std::vector<std::string> words = normalizedWords(text);
        if (words.size() < 3)
        {
            return false;
        }

        // Every run of three consecutive words is checked, so the phrase is found in the
        // middle of a longer message, but the three words must stay in order.
        for (size_t i = 0; i + 2 < words.size(); ++i)
        {
            const std::string canonical = words[i] + " " + words[i + 1] + " " + words[i + 2];
            if (hashTriple(salt, canonical) == stored)
            {
                runSafewordResponse();
                return true;
            }
        }
        return false;
    }
}
