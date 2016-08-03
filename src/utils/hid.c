/****************************************************************************
 * Copyright (C) 2016 Maschell
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "hid.h"
#include "common/retain_vars.h"
#include "utils/logger.h"

void hid_init(){
    HIDSetup();
    HIDAddClient(&gHIDClient, my_attach_cb);
}

void hid_deinit(){
    HIDDelClient(&gHIDClient);
}


#define SWAP16(x) ((x>>8) | ((x&0xFF)<<8))
#define SWAP8(x) ((x>>4) | ((x&0xF)<<4))
int my_attach_cb(HIDClient *p_client, HIDDevice *p_device, unsigned int attach)
{
    if(attach){
        log_printf("vid %04x pid %04x connected\n", SWAP16(p_device->vid),SWAP16(p_device->pid));
        log_printf("interface index  %02x\n", p_device->interface_index);
        log_printf("sub class        %02x\n", p_device->sub_class);
        log_printf("protocol         %02x\n", p_device->protocol);
        log_printf("max packet in    %02x\n", p_device->max_packet_size_rx);
        log_printf("max packet out   %02x\n", p_device->max_packet_size_tx);
    }
    if(!attach){
        log_printf("vid %04x pid %04x disconnected\n", SWAP16(p_device->vid),SWAP16(p_device->pid));
    }

    if(attach){
        int bufSize = 64;

        unsigned char *buf = memalign(64,bufSize);
        memset(buf,0,bufSize);
        my_cb_user *usr = memalign(64,sizeof(my_cb_user));

        usr->buf = buf;
        usr->device = p_device;
        usr->transfersize = p_device->max_packet_size_rx;
        usr->handle = p_device->handle;

        HIDRead(p_device->handle, usr->buf, p_device->max_packet_size_rx, my_read_cb, usr);

        return HID_DEVICE_ATTACH;
    }else{
        my_cb_user * user_data = NULL;

        if(user_data){
            if(user_data->buf){
                free(user_data->buf);
                user_data->buf = NULL;
            }
            free(user_data);
            user_data = NULL;
        }
    }
	return HID_DEVICE_DETACH;
}



//!----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//! GC-Adapter
//!----------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void my_read_cb(unsigned int handle, int error, unsigned char *buf, unsigned int bytes_transfered, void *p_user)
{
	if(error == 0 && p_user != NULL )
	{
		my_cb_user *usr = (my_cb_user*)p_user;
		hid_callback_data = usr;
		unsigned char*  buffer = usr->buf;
		log_printf("     %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7], buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15]);

        HIDRead(handle, usr->buf, bytes_transfered, my_read_cb, usr);
	}
}
