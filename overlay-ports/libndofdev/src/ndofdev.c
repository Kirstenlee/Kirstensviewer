/*
 @file ndofdev.c
 @brief Part of libndofdev fully implemented in a cross platform way.
 
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
#include <assert.h>
#include "ndofdev_external.h"
#include "ndofdev_internal.h"

#if TARGET_OS_MAC
#include "ndofdev_internal_osx.h"
#elif defined(_WIN32) || defined(WIN32)
#include "ndofdev_internal_win.h"
#else /* linux */
#include "ndofdev_internal_linux.h"
#endif

/* --------------------------------------------------------------------------
    Global/Static Variables                                                   */

/*	This library maintains a list of all the allocated NDOF_Device structures
	to reliably and transparently maintain memory.  */
NDOF_DeviceListNode *g_ndof_list_head = NULL;
static int s_ndof_list_len = 0;

/* --------------------------------------------------------------------------
	Static Function Prototypes                                                */

static void ndof_devdispose(NDOF_Device *dev);

/* -------------------------------------------------------------------------- */
NDOF_Device *ndof_create()
{
    NDOF_Device *dev = (NDOF_Device *) malloc(sizeof(NDOF_Device));
    
    /* head insert */
    NDOF_DeviceListNode *node = 
        (NDOF_DeviceListNode*) malloc(sizeof(NDOF_DeviceListNode));
    node->dev = dev;
    node->next = g_ndof_list_head;
    g_ndof_list_head = node;
    s_ndof_list_len++;
	
    memset(dev, 0, sizeof(NDOF_Device));
    dev->btn_count = -1;  /* we could have an ndof device with no btns */
    dev->axes_min = -500; /* reasonable default value */
    dev->axes_max = +500; /* reasonable default value */
    
    /* initialize platform data */
    dev->private_data = 
        (NDOF_DevicePrivate*) malloc(sizeof(NDOF_DevicePrivate));
	memset(dev->private_data, 0, sizeof(NDOF_DevicePrivate));
    return dev;
}

/* -------------------------------------------------------------------------- */
void ndof_destroy(NDOF_Device *in_device)
{
	NDOF_DeviceListNode *prev = NULL, *node = g_ndof_list_head;
	while (node)
	{
		if (node->dev == in_device)
		{
			if (prev)
				prev->next = node->next;
			else
				g_ndof_list_head = node->next; /* head delete */

			free(node);
			ndof_devdispose(in_device);
			break;
		}
		else
		{
			prev = node;
			node = node->next;
		}
	}
}

/* -------------------------------------------------------------------------- */
static void ndof_devdispose(NDOF_Device *dev)
{
    ndof_dev_private_dispose((NDOF_DevicePrivate *)dev->private_data);
    free(dev);
}

/* -------------------------------------------------------------------------- */
void ndof_libcleanup()
{
    NDOF_DeviceListNode *node;

#ifdef NDOF_DEBUG
    fprintf(NDOF_DEBUG, "libndofdev: cleaning up...\n");
#endif

    while (g_ndof_list_head)
    {
        ndof_devdispose(g_ndof_list_head->dev);
        node = g_ndof_list_head->next;
        free(g_ndof_list_head);
        g_ndof_list_head = node;
    }
    
    ndof_cleanup_internal();

#ifdef NDOF_DEBUG
    fprintf(NDOF_DEBUG, "libndofdev: clean up completed.\n");
#endif
}

/* -------------------------------------------------------------------------- */
unsigned char ndof_match(NDOF_Device *dev1, NDOF_Device *dev2)
{
    if (dev1 && dev2)
    {
        size_t lenm1 = strlen(dev1->manufacturer);
        size_t lenp1 = strlen(dev1->product);
        
        if (strncmp(dev1->manufacturer, dev2->manufacturer, lenm1) == 0
            && strncmp(dev1->product, dev2->product, lenp1) == 0
            && dev1->axes_count == dev2->axes_count 
            && dev1->btn_count == dev2->btn_count)
        {
            return ndof_match_private((NDOF_DevicePrivate*)dev1->private_data, 
                                      (NDOF_DevicePrivate*)dev2->private_data);
        }
    }
    
    return 0;
}

/* -------------------------------------------------------------------------- */
void ndof_dump_list(FILE* stream)
{
    NDOF_DeviceListNode *node = g_ndof_list_head;
    
    fprintf(stream, "libndofdev: List of currently used NDOF devices:\n");
 
    while (node)
    {
        ndof_dump(stream, node->dev);
        node = node->next;
    }
}

/* -------------------------------------------------------------------------- */
void ndof_dump(FILE* stream, NDOF_Device *dev)
{
    if (dev == NULL)
    {
        fprintf(stream, "libndofdev: NULL device");
        return;
    }
    
	fprintf(stream, "libndofdev: manufacturer=%s; product=%s; axes_count=%d; " \
			"btn_count=%d; min=%ld; max=%ld; absolute=%d; valid=%d; " \
            "private_data=%p;\n", 
			dev->manufacturer, dev->product, dev->axes_count, dev->btn_count, 
			dev->axes_min, dev->axes_max, dev->absolute, dev->valid, 
            dev->private_data);
	
#if TARGET_OS_MAC
    {
        int i;
        NDOF_DevicePrivate *priv = (NDOF_DevicePrivate*)dev->private_data;
        fprintf(stream, "    curr_loc_id=%08lX\n", priv->curr_loc_id);
        
        fprintf(stream, "    scales:  [");
        for (i=0; i<NDOF_MAX_AXES_COUNT; i++)
             fprintf(stream, "%f ", priv->scale[i]);
        
        fprintf(stream, "]\n    offsets: [");
        for (i=0; i<NDOF_MAX_AXES_COUNT; i++)
            fprintf(stream, "%f ", priv->offset[i]);

        fprintf(stream, "]\n");
    }
#elif defined(_WIN32) || defined(WIN32)
	fprintf(stream, "type=%hd; subtype=%hd\n", 
			((NDOF_DevicePrivate*)dev->private_data)->type,
			((NDOF_DevicePrivate*)dev->private_data)->subtype);
#endif	
}

/* -------------------------------------------------------------------------- */
#if LIBNDOF_UNIT_TESTS
void test_device_list_add()
{
	/* Run this test before the others */
	
    NDOF_Device *dev1, *dev2;
	NDOF_DeviceListNode *node;
	int n, m, err1, err2;
	
    fprintf(stderr, "____ test_device_list_add ____________________________\n");
	
	/* count device list nodes before adding new nodes */ 
	node = g_ndof_list_head;
	n = 0;
	while (node)
	{
		n++;
		node = node->next;
	}		
	
    dev1 = ndof_create();
    dev2 = ndof_create();
    assert(dev1);
    assert(dev2);
    
	/* test length of device list after additions */
	node = g_ndof_list_head;
	m = 0;
	while (node)
	{
		m++;
		dev1 = node->dev;
		ndof_dump(stderr, dev1);
		node = node->next;
	}		
	assert(m == n + 2);
	assert(m == s_ndof_list_len);
	
	/* compare 2 additions, they should be the same (init'ed to 0's) */
	assert(strcmp(dev1->manufacturer, dev2->manufacturer) == 0);
	assert(strcmp(dev1->manufacturer, "") == 0);
	assert(strcmp(dev1->product, dev2->product) == 0);
	assert(strcmp(dev1->product, "") == 0);
	assert(dev1->axes_count == dev2->axes_count);
	assert(dev1->axes_count == 0);
	assert(dev1->btn_count == dev2->btn_count);
	assert(dev1->btn_count == -1);
    assert(dev1->axes_min < 0);
    assert(dev1->axes_max > 0);
    assert(dev1->axes_min == dev2->axes_min);
    assert(dev1->axes_max == dev2->axes_max);
	
	assert(g_ndof_list_head);
	assert(g_ndof_list_head->next);
	assert(g_ndof_list_head->next->next == NULL);
	
	/* now init the devices proxies */
	err1 = ndof_init_first(dev1, NULL);
    err2 = ndof_init_first(dev2, NULL);
	assert(err1 == 0);
	assert(err2 == 0);
	
	/* compare 2 additions, they should be the same (and NOT init'ed to 0's) */
    #if !defined(_WIN32) && !defined(WIN32)
	assert(strcmp(dev1->manufacturer, dev2->manufacturer) == 0);
	assert(strcmp(dev1->manufacturer, "") != 0);
    #endif
    assert(strcmp(dev1->product, dev2->product) == 0);
	assert(strcmp(dev1->product, "") != 0);
	assert(dev1->axes_count == dev2->axes_count);
	assert(dev1->axes_count != 0);
	assert(dev1->btn_count == dev2->btn_count);
	assert(dev1->btn_count != -1);
	
	fprintf(stderr, "  done\n");
}

#endif
