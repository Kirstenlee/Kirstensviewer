/**
 * @file krlvbehaviours.h
 * @brief Declares one registration function per RLVa command category.
 * krlvhandler.cpp's init() calls each of these explicitly, in a fixed
 * order - see that file for why this is deliberately not static-
 * initializer self-registration.
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

#ifndef KRLV_KRLVBEHAVIOURS_H
#define KRLV_KRLVBEHAVIOURS_H

class KRlvHandler;

// One registration function per krlv/behaviours/*.cpp file. A command with
// no registered handler is ignored. See krlv/README.md's Behaviour
// Categories table for the command list.
void krlv_register_version_commands(KRlvHandler& handler);
void krlv_register_blacklist_commands(KRlvHandler& handler);
void krlv_register_misc_commands(KRlvHandler& handler);
void krlv_register_movement_commands(KRlvHandler& handler);
void krlv_register_camera_commands(KRlvHandler& handler);
void krlv_register_chat_commands(KRlvHandler& handler);
void krlv_register_teleport_commands(KRlvHandler& handler);
void krlv_register_inventory_commands(KRlvHandler& handler);
void krlv_register_sitting_commands(KRlvHandler& handler);
void krlv_register_attachment_commands(KRlvHandler& handler);
void krlv_register_shared_folder_commands(KRlvHandler& handler);
void krlv_register_touch_commands(KRlvHandler& handler);
void krlv_register_location_commands(KRlvHandler& handler);
void krlv_register_name_commands(KRlvHandler& handler);
void krlv_register_group_commands(KRlvHandler& handler);
void krlv_register_viewer_control_commands(KRlvHandler& handler);
void krlv_register_unofficial_commands(KRlvHandler& handler);

#endif // KRLV_KRLVBEHAVIOURS_H
