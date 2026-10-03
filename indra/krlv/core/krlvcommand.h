/**
 * @file krlvcommand.h
 * @brief KRLV command parsing - splits a raw "@name[:option]=param" string
 * into its parts.
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

#ifndef KRLV_KRLVCOMMAND_H
#define KRLV_KRLVCOMMAND_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// KRLV: one parsed command, e.g. "@sendim:12345678-...=n" splits into
// behaviour="sendim", option="12345678-...", param="n". A bare
// "@versionnum=2222" has an empty option and param="2222". The behaviour
// name is stored lowercase; option/param keep their original case (some
// carry UUIDs, folder names, or numeric values that must round-trip
// unchanged).
struct KRlvCommand
{
    std::string behaviour;
    std::string option;
    std::string param;

    // KRLV: same span as `behaviour`, before lowercasing. Exact-match and
    // prefix dispatch both key off the lowercase `behaviour` (so
    // "@Sendim" and "@sendim" hit the same handler) - but a handful of
    // prefix commands (setdebug_<setting>, getenv_<setting>, ...) glue a
    // dynamic, case-sensitive name (a gSavedSettings control name) onto
    // the prefix with no ":option" separator, so the real setting name is
    // only recoverable from this field, not the lowercased one.
    std::string behaviourRaw;

    bool hasOption() const { return !option.empty(); }
};

namespace KRlv
{
    // Splits one "@..." token (no leading '@', no surrounding whitespace)
    // into a KRlvCommand. Returns std::nullopt for anything that isn't
    // shaped like "name[:option]=param" - callers decide what a malformed
    // command means (RLVa's own spec says a script sending garbage gets
    // silently ignored, not an error reply).
    std::optional<KRlvCommand> parseCommand(std::string_view token);

    // A chat message from an object can carry several "@a=y,@b=n" commands
    // separated by commas. Returns every piece parsed, or an empty vector if
    // ANY piece is not a command, so ordinary chat containing '@' is never
    // mistaken for a command line.
    std::vector<KRlvCommand> parseMessage(std::string_view message);
}

#endif // KRLV_KRLVCOMMAND_H
