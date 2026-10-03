/**
 * @file krlvcommand.cpp
 * @brief KRLV command parsing implementation.
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

#include "krlvcommand.h"

#include <algorithm>
#include <cctype>

namespace
{
    bool isNameChar(char c)
    {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    }

    std::string toLowerAscii(std::string_view sv)
    {
        std::string out(sv);
        std::transform(out.begin(), out.end(), out.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }
}

namespace KRlv
{
    std::optional<KRlvCommand> parseCommand(std::string_view token)
    {
        // Trim incidental whitespace - scripts are not always tidy.
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))
        {
            token.remove_prefix(1);
        }
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
        {
            token.remove_suffix(1);
        }
        if (token.empty())
        {
            return std::nullopt;
        }

        // "@behaviour[:option]=param" - the '=' (if present) always
        // separates the command name/option from the param; a command
        // like "@clear" or "@version" (IM-only form) has no '=' at all.
        std::string_view left = token;
        std::string_view param;
        if (const size_t eq = token.find('='); eq != std::string_view::npos)
        {
            left = token.substr(0, eq);
            param = token.substr(eq + 1);
        }

        std::string_view behaviour = left;
        std::string_view option;
        if (const size_t colon = left.find(':'); colon != std::string_view::npos)
        {
            behaviour = left.substr(0, colon);
            option = left.substr(colon + 1);
        }

        if (behaviour.empty())
        {
            return std::nullopt;
        }

        KRlvCommand cmd;
        cmd.behaviour = toLowerAscii(behaviour);
        cmd.behaviourRaw = std::string(behaviour);
        cmd.option = std::string(option);
        cmd.param = std::string(param);
        return cmd;
    }

    std::vector<KRlvCommand> parseMessage(std::string_view message)
    {
        // Every comma-separated piece must be a command: '@', a name of [A-Za-z0-9_]
        // (case-insensitive, so setting names such as setdebug_RenderDeferred work),
        // then end of piece, ':' or '='. Pieces are not trimmed. One failing piece
        // makes the whole message a non-command, so an empty result is returned.
        std::vector<KRlvCommand> out;
        size_t pos = 0;
        while (true)
        {
            const size_t comma = message.find(',', pos);
            const std::string_view piece = (comma == std::string_view::npos)
                ? message.substr(pos)
                : message.substr(pos, comma - pos);

            if (piece.size() < 2 || piece.front() != '@')
            {
                return {};
            }
            size_t nameEnd = 1;
            while (nameEnd < piece.size() && isNameChar(piece[nameEnd]))
            {
                ++nameEnd;
            }
            if (nameEnd == 1 || (nameEnd < piece.size() && piece[nameEnd] != ':' && piece[nameEnd] != '='))
            {
                return {};
            }
            auto cmd = parseCommand(piece.substr(1));
            if (!cmd)
            {
                return {};
            }
            out.push_back(std::move(*cmd));

            if (comma == std::string_view::npos)
            {
                break;
            }
            pos = comma + 1;
        }
        return out;
    }
}
