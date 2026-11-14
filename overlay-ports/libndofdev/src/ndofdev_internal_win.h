/*
 @file ndofdev_win.c
 @brief Windows specific header.
 Created by Ettore Pasquini on 8/7/07.
 
 Copyright (c) 2007, 3Dconnexion, Inc. - All rights reserved.
 
 Redistribution and use in source and binary forms, with or without 
 modification, are permitted provided that the following conditions are met:
 - Redistributions of source code must retain the above copyright notice, 
   this list of conditions and the following disclaimer.
 - Redistributions in binary form must reproduce the above copyright notice, 
   this list of conditions and the following disclaimer in the documentation 
   and/or other materials provided with the distribution.
 - Neither the name of the 3Dconnexion, Inc. nor the names of its contributors 
   may be used to endorse or promote products derived from this software 
   without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR 
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __ndofdev_internal_windoze_h__
#define __ndofdev_internal_windoze_h__

#include <InitGuid.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NDOF_DevicePrivate {
    LPDIRECTINPUTDEVICE8 dev;
	short type;
	short subtype;
	//long measured_max;
} NDOF_DevicePrivate;

void ndof_cleanup_internal();

/** Makes sure everything related to `priv' is tidily disposed. */
void ndof_dev_private_dispose(NDOF_DevicePrivate *priv);

/** Returns 1 if d1 and d2 are the same device at the same port. */
unsigned char ndof_match_private(NDOF_DevicePrivate *d1, NDOF_DevicePrivate *d2);

#ifdef __cplusplus
}
#endif
	
#endif /* __ndofdev_internal_windoze_h__ */
