/**
 * @file llmachineid.h
 * @brief retrieves unique machine ids
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#ifndef LL_LLMACHINEID_H
#define LL_LLMACHINEID_H


class LLMachineID
{
public:
    LLMachineID() = default;
    virtual ~LLMachineID() = default;

    /**
     * Initializes the machine ID by reading the Windows MachineGuid from
     * HKLM\SOFTWARE\Microsoft\Cryptography and deriving a 6-byte digest.
     * Must be called once at startup before any threads are created.
     */
    static S32 init();

    /**
     * Copies the unique machine ID into the provided buffer.
     * @param unique_id[out] Output buffer to receive the ID bytes.
     * @param len Size of the output buffer (must be >= 6).
     * @return 1 if valid data was copied, 0 otherwise.
     */
    static S32 getUniqueID(unsigned char *unique_id, size_t len);

    /// Legacy fallback -- retained for API compatibility.
    static S32 getLegacyID(unsigned char *unique_id, size_t len);

private:
    LLMachineID(const LLMachineID&) = delete;
    LLMachineID& operator=(const LLMachineID&) = delete;
};


#endif // LL_LLMACHINEID_H
