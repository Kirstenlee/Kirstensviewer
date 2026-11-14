/*
 @file ndofdev_win.c
 @brief Windows implementation.
 Created by Ettore Pasquini on 8/3/07.
 
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

#include <stdio.h>
#include <string.h>
#include <tchar.h>
#include <assert.h>
#include "ndofdev_external.h"
#include "ndofdev_internal_win.h"

static LPDIRECTINPUT8 gDI = NULL;   // DI interface
static HWND gDIWnd = NULL;          // window associated with DI

#ifdef NDOF_DEBUG
void ndof_print_deviceinstance_info(const DIDEVICEINSTANCE *dev_info);
#endif

/* -------------------------------------------------------------------------- */
BOOL CALLBACK EnumNDOFDeviceCallback(const DIDEVICEINSTANCE *inst, VOID *inCtx)
{
    if (inCtx == NULL)
        return DIENUM_CONTINUE;

	HRESULT hr;
    NDOF_Device *dev = (NDOF_Device *)inCtx;
    NDOF_DevicePrivate *priv = (NDOF_DevicePrivate *)dev->private_data;

	priv->type = GET_DIDEVICE_TYPE(inst->dwDevType);
	priv->subtype = GET_DIDEVICE_SUBTYPE(inst->dwDevType);

	// obtain an interface to the enumerated ndof device
    hr = gDI->CreateDevice(inst->guidInstance, &priv->dev, NULL);

	//priv->dev->GetDeviceInfo(const_cast<DIDEVICEINSTANCE*>(inst));
	//ndof_print_deviceinstance_info(inst);
	
    strncpy(dev->product, inst->tszProductName, sizeof(dev->product));

	// if it failed we can't use this device, so continue to next one
	if (FAILED(hr)) 
		return DIENUM_CONTINUE;

	// stop enumeration (just taking the first we get)
	return DIENUM_STOP;
}

/* -------------------------------------------------------------------------- */
BOOL CALLBACK EnumNDOFObjectsCallback(const DIDEVICEOBJECTINSTANCE* inst,
                                      VOID* user_data)
{
	if (inst->dwType & DIDFT_AXIS)
	{
        HRESULT hr = DI_OK;
        NDOF_Device *d = (NDOF_Device *)user_data;
        LPDIRECTINPUTDEVICE8 diDev = ((NDOF_DevicePrivate*)d->private_data)->dev;
		DIPROPRANGE diprg;
        diprg.diph.dwSize       = sizeof(DIPROPRANGE); 
		diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER); 
		diprg.diph.dwHow        = DIPH_BYID; 
		diprg.diph.dwObj        = inst->dwType; // specify the enumerated axis

		// Set the range for the axis
		diprg.lMin              = d->axes_min;
        diprg.lMax              = d->axes_max;
		hr = diDev->SetProperty(DIPROP_RANGE, &diprg.diph);
		assert(hr == DI_OK);
		
#if 0
		/*switch (hr)
        {
        case DI_OK:
            fprintf(stderr, " DI_OK\n");
            break;
        case DI_PROPNOEFFECT:
            fprintf(stderr, " DI_PROPNOEFFECT\n");
            break;
        case DIERR_INVALIDPARAM:
            fprintf(stderr, "SetProperty failed: DIERR_INVALIDPARAM \n");
            break;
        case  DIERR_NOTINITIALIZED:
            fprintf(stderr, "SetProperty failed: DIERR_NOTINITIALIZED\n");
            break;
        case DIERR_OBJECTNOTFOUND:
            fprintf(stderr, "SetProperty failed: DIERR_OBJECTNOTFOUND\n");
            break;
        case DIERR_UNSUPPORTED:
            fprintf(stderr, "SetProperty failed: DIERR_UNSUPPORTED\n");
            break;
        }*/

        // try to sense the axis range
        HRESULT hr1, hr2, hr3;
		diprg.diph.dwSize = sizeof(DIPROPRANGE);
        diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        diprg.diph.dwHow = DIPH_BYID; //DIPH_BYUSAGE;
        //diprg.lMax = 100;
        //diprg.lMin = -100;
        //diprg.diph.dwObj = DIJOFS_X;
        //hr = diDev->SetProperty(DIPROP_RANGE, &diprg.diph);
        //diprg.diph.dwObj = DIJOFS_Y;
        //hr1 = diDev->SetProperty(DIPROP_RANGE, &diprg.diph);
        //fprintf(stderr, "hr1=%d; hr2=%d\n", hr, hr2);
		
        hr1 = diDev->GetProperty(DIPROP_RANGE, (LPDIPROPHEADER)&diprg.diph);
        if (SUCCEEDED(hr))
            fprintf(stderr, "range: (%ld, %ld)\n", diprg.lMin, diprg.lMax);
        else
            fprintf(stderr, "get range ERROR: (%ld, %ld)\n", 
                    diprg.lMin, diprg.lMax);

        hr2 = diDev->GetProperty(DIPROP_LOGICALRANGE , (LPDIPROPHEADER)&diprg.diph);
        if (SUCCEEDED(hr2))
            fprintf(stderr, "logical range: (%ld, %ld)\n", 
                    diprg.lMin, diprg.lMax);
        else
            fprintf(stderr, "get logical range ERROR: (%ld, %ld)\n", 
                    diprg.lMin, diprg.lMax);

        hr3 = diDev->GetProperty(DIPROP_PHYSICALRANGE, (LPDIPROPHEADER)&diprg.diph);
        if (SUCCEEDED(hr2))
            fprintf(stderr, "phyical range: (%ld, %ld)\n", 
                    diprg.lMin, diprg.lMax);
        else
            fprintf(stderr, "get phyical range ERROR: (%ld, %ld)\n", 
                    diprg.lMin, diprg.lMax);

		assert((hr1 == DI_OK || hr1 == S_FALSE) || 
		       (hr2 == DI_OK || hr2 == S_FALSE) || 
			   (hr3 == DI_OK || hr3 == S_FALSE));
#endif

        if (FAILED(hr))
            return DIENUM_STOP;
    }

	return DIENUM_CONTINUE;
}

/* -------------------------------------------------------------------------- */
int ndof_libinit(NDOF_DeviceAddCallback in_add_cb, 
                 NDOF_DeviceRemovalCallback in_removal_cb,
                 void *param)
{
    fprintf(stderr, "libndofdev: initializing...\n");

    if (param && *((LPDIRECTINPUT8 *)param))
    {
        gDI = *((LPDIRECTINPUT8 *)param);
    }
    else if (DirectInput8Create(GetModuleHandle(NULL), 
                                DIRECTINPUT_VERSION,
                                IID_IDirectInput8, 
                                (VOID**)&gDI, 
                                NULL) != DI_OK)
    {
        fprintf(stderr, "libndofdev: Error initializing DirectInput: " \
                "Direct8InputCreate failed.\n");
        return -1;
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
int ndof_init_first(NDOF_Device *dev, void *diHandle)
{
    int notfound = -1;
    HRESULT hr;
    LPDIRECTINPUTDEVICE8 diDev;

    // if a DirectInput handle is being passed in, try to use it and exit if ok
    if (diHandle && *((LPDIRECTINPUTDEVICE8 *)diHandle))
    {
        NDOF_DevicePrivate *priv;

        diDev = *((LPDIRECTINPUTDEVICE8 *)diHandle);
        priv = (NDOF_DevicePrivate *)dev->private_data;
        //priv->measured_max = dev->axes_max;
        priv->dev = diDev;

	    if (diDev->Acquire() == DI_OK)
        {
            dev->axes_count = 6; // can we read it from DI ??
            dev->btn_count = 32; // see diDev->GetDeviceInfo
            notfound = 0;        // the handle can be used! let's exit then.
        }
    }
    
    // if the input DI handle didn't work, or if none was passed, create new one
    while(notfound)
	{
		// Look for a simple joystick we can use for this program.
		if (FAILED(hr = gDI->EnumDevices(DI8DEVCLASS_GAMECTRL, 
		                                 EnumNDOFDeviceCallback, dev, 
                                         DIEDFL_ATTACHEDONLY)))
			break;
		
        diDev = ((NDOF_DevicePrivate *)dev->private_data)->dev;
        if (diDev == NULL)
			break;

        // Set the data format to "simple joystick" - a predefined data format.
        // A data format specifies which controls on a device we are interested 
        // in, and how they should be reported. This tells DInput that we'll be
        // passing a DIJOYSTATE2 struct to IDirectInputDevice::GetDeviceState().
        if (FAILED(hr = diDev->SetDataFormat(&c_dfDIJoystick)))
			break;

		/* ---- NOTE: do we need to do this? SL doesn't do it.

        // Set the cooperative level to let DI know how this device should
        // interact with the system and with other DInput applications.
	    HWND w;
        if (gDIWnd == NULL)
            w = ::GetActiveWindow();
        else
            w = gDIWnd;

        if (w)
        {
            if (FAILED(hr = diDev->
	    	        SetCooperativeLevel(w, DISCL_EXCLUSIVE | DISCL_FOREGROUND)))
            {
                fprintf(stderr, 
                    _T("DirectInput SetCooperativeLevel failed! hwnd=%p; hr=%d\n"), 
                    w, hr);
                break;
            }
        }
		------- */

		if (FAILED(diDev->EnumObjects(EnumNDOFObjectsCallback, dev, DIDFT_ALL)))
			break;

		if (diDev->Acquire() == DI_OK)
        {
            dev->axes_count = 6; // can we read it from DI ??
            dev->btn_count = 32; // see diDev->GetDeviceInfo
            notfound = 0;
        }
		break;
	}
    
    if (notfound == 0)
	{
        fprintf(stderr, "libndofdev: using device: " \
                "manufacturer=%s; product=%s; axes_count=%d; btn_count=%d; " \
                "opaque=%p; valid=%d\n", dev->manufacturer, dev->product,
                dev->axes_count, dev->btn_count, dev->private_data, 
#if TARGET_OS_MAC
                HIDIsValidDevice(((NDOF_DevicePrivate*)dev->private_data)->dev));
#else
                1);
#endif
	}

    return notfound;
}

/* -------------------------------------------------------------------------- */
void ndof_update(NDOF_Device *in_dev)
{
    NDOF_DevicePrivate *priv = (NDOF_DevicePrivate*)in_dev->private_data;
    static long last_axes[] = {0,0,0,0,0,0};
    
    if (priv == NULL || priv->dev == NULL)
        return; // attempting to read status from uninitialized structure
    
    HRESULT hr;
	DIJOYSTATE js; // DirectInput joystick state

	hr = priv->dev->Poll();
	if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
	{
		hr = priv->dev->Acquire();
		return;
	}
	else if (hr == DIERR_NOTINITIALIZED)
		return;

	if( FAILED(hr = priv->dev->GetDeviceState(sizeof(DIJOYSTATE), &js)))
		return; // The device should have been acquired during the Poll()

	in_dev->axes[0] = js.lX;
	in_dev->axes[1] = js.lY;
	in_dev->axes[2] = js.lZ;
	in_dev->axes[3] = js.lRx;
	in_dev->axes[4] = js.lRy;
	in_dev->axes[5] = js.lRz;

	if (!in_dev->absolute)
	{
		for (int i = 0; i < in_dev->axes_count; i++)
		{
            long tmp = in_dev->axes[i];
            in_dev->axes[i] -= last_axes[i];
            last_axes[i] = tmp;

			/*
            static float scale = (float)in_dev->axes_max / priv->measured_max;
			if (in_dev->scaling != kNoScaling)
			{
				if (in_dev->scaling == kAutoScalingToMax)
				{
					if (labs(in_dev->axes[i]) > priv->measured_max)
					{
						priv->measured_max = labs(in_dev->axes[i]);
						scale = (float)in_dev->axes_max / (float)priv->measured_max;
					}
					in_dev->axes[i] = scale * in_dev->axes[i];
				}
				else // kSaturatedScalingToMax
				{
					if (in_dev->axes[i] > in_dev->axes_max)
						in_dev->axes[i] = in_dev->axes_max;

					if (in_dev->axes[i] < in_dev->axes_min)
						in_dev->axes[i] = in_dev->axes_min;
				}
			}
			*/
		}
	}

    #ifdef NDOF_DEBUG
    if (in_dev->axes[0] || in_dev->axes[1] || in_dev->axes[2] 
        || in_dev->axes[3] || in_dev->axes[4] || in_dev->axes[5])
    {
        fprintf(NDOF_DEBUG, "ndof_update: %ld %ld %ld %ld %ld %ld\n", 
                in_dev->axes[0], in_dev->axes[1], in_dev->axes[2], 
                in_dev->axes[3], in_dev->axes[4], in_dev->axes[5]);
    }
    #endif

	for (int i = 0; i < in_dev->btn_count; i++)
		in_dev->buttons[i] = (js.rgbButtons[i] == 0x80);
}

/* -------------------------------------------------------------------------- */
void ndof_cleanup_internal()
{
    if (gDI) 
    { 
        gDI->Release(); 
        gDI = NULL; 
    }
}

/* -------------------------------------------------------------------------- */
void ndof_dev_private_dispose(NDOF_DevicePrivate *priv)
{
    if (priv && priv->dev)
    {
        priv->dev->Unacquire();

        // Release any DirectInput objects.
        priv->dev->Release();
    
        free(priv);
    }
}

/* -------------------------------------------------------------------------- */
unsigned char ndof_match_private(NDOF_DevicePrivate *d1, NDOF_DevicePrivate *d2)
{
	return (d1 && d2 && d1->type == d2->type && d1->subtype == d2->subtype);
}

#ifdef NDOF_DEBUG
/* -------------------------------------------------------------------------- */
void ndof_print_deviceinstance_info(const DIDEVICEINSTANCE *dev_info)
{
	fprintf(NDOF_DEBUG, "Product Name: %s;\n Instance Name: %s;\n " \
			"size=%lu;\n HID Usage Page code=%d;\n HID Usage code=%d;\n", 
			dev_info->tszProductName, dev_info->tszInstanceName, 
			dev_info->dwSize, dev_info->wUsagePage, dev_info->wUsage);

	short dev_type = GET_DIDEVICE_TYPE(dev_info->dwDevType);
	short dev_subtype = GET_DIDEVICE_SUBTYPE(dev_info->dwDevType);
	
	switch (dev_type)
	{
	case DI8DEVTYPE_1STPERSON:
		fprintf(NDOF_DEBUG, "Type: %lu (%s)\n", dev_type, "1st person");
		break;
	case DI8DEVTYPE_DEVICE:
		fprintf(NDOF_DEBUG, "Type: %lu (%s)\n", dev_type, "Unknown category");
		break;
	case DI8DEVTYPE_DEVICECTRL:
		fprintf(NDOF_DEBUG, "Type: %lu (%s)\n", dev_type, "Device Control");
		break;
	case DI8DEVTYPE_DRIVING:
		fprintf(NDOF_DEBUG, "Type: %lu (%s)\n", dev_type, "Driving");
		break;
	case DI8DEVTYPE_FLIGHT: 
		fprintf(NDOF_DEBUG, "Type: %lu (%s)\n", dev_type, "Flight");
		break;
	case DI8DEVTYPE_GAMEPAD:
		fprintf(NDOF_DEBUG, "Type: %lu (%s)\n", dev_type, "Gamepad");
		break;
	case DI8DEVTYPE_JOYSTICK:
		fprintf(NDOF_DEBUG, "Type: %lu (%s)\n", dev_type, "Joystick");
		break;
	case DI8DEVTYPE_KEYBOARD: 
		fprintf(NDOF_DEBUG, "Type: %lu (%s)\n", dev_type, "Keyboard");
		break;
	case DI8DEVTYPE_MOUSE:
		fprintf(NDOF_DEBUG, "Type: %lu (%s)\n", dev_type, "Mouse");
		break;
	case DI8DEVTYPE_REMOTE:
		fprintf(NDOF_DEBUG, "Type: %lu (%s)\n", dev_type, "Remote");
		break;
	case DI8DEVTYPE_SCREENPOINTER:
		fprintf(NDOF_DEBUG, "Type: %lu (%s)\n", dev_type, "Screen Pointer");
		break;
	case DI8DEVTYPE_SUPPLEMENTAL:
		fprintf(NDOF_DEBUG, "Type: %lu (%s)\n", dev_type, "Supplemental");
		break;
	default:
		fprintf(NDOF_DEBUG, "Type: %lu (%s)\n", dev_type, "Undefined");
		break;
	}
}

#endif
