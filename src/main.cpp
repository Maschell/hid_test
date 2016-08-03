#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include "main.h"
#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/gx2_functions.h"
#include "dynamic_libs/syshid_functions.h"
#include "dynamic_libs/vpad_functions.h"
#include "dynamic_libs/socket_functions.h"
#include "dynamic_libs/sys_functions.h"
#include "utils/hid.h"
#include "common/retain_vars.h"
#include "utils/logger.h"

#define PRINT_TEXT1(x, y, str) { OSScreenPutFontEx(1, x, y, str); OSScreenPutFontEx(0, x, y, str); }
#define PRINT_TEXT2(x, y, _fmt, ...) { __os_snprintf(msg, 80, _fmt, __VA_ARGS__); OSScreenPutFontEx(0, x, y, msg);OSScreenPutFontEx(1, x, y, msg); }

/* Entry point */
extern "C" int Menu_Main(void)
{
    //!*******************************************************************
    //!                   Initialize function pointers                   *
    //!*******************************************************************
    //! do OS (for acquire) and sockets first so we got logging
    InitOSFunctionPointers();
    InitSocketFunctionPointers();
    InitSysFunctionPointers();
    InitSysHIDFunctionPointers();
    InitVPadFunctionPointers();

    log_init("192.168.0.181");

    log_printf("HID-TEST by Maschell. Building time: %s %s\n\n",__DATE__,__TIME__);

    hid_init();

    DisplayScreen();

    hid_deinit();
    log_deinit();
    return EXIT_SUCCESS;
}
#define SWAP16(x) ((x>>8) | ((x&0xFF)<<8))
#define SWAP8(x) ((x>>4) | ((x&0xF)<<4))
void DisplayScreen(){


    // Prepare screen
    int screen_buf0_size = 0;

    // Init screen and screen buffers
    OSScreenInit();
    screen_buf0_size = OSScreenGetBufferSizeEx(0);
    OSScreenSetBufferEx(0, (void *)0xF4000000);
    OSScreenSetBufferEx(1, (void *)(0xF4000000 + screen_buf0_size));

    OSScreenEnableEx(0, 1);
    OSScreenEnableEx(1, 1);

    // Clear screens
    OSScreenClearBufferEx(0, 0);
    OSScreenClearBufferEx(1, 0);

    // Flip buffers
    OSScreenFlipBuffersEx(0);
    OSScreenFlipBuffersEx(1);

    char* msg = (char*) malloc(80);

    VPADData vpad_data;
    int error;
    do{
        // Refresh screens
        OSScreenFlipBuffersEx(1);
        OSScreenClearBufferEx(1, 0);
        OSScreenFlipBuffersEx(0);
        OSScreenClearBufferEx(0, 0);

        // Read vpad
        VPADRead(0, &vpad_data, 1, &error);
        int i = 0;
        PRINT_TEXT2(0, i, "HID-TEST - by Maschell - %s %s",__DATE__,__TIME__); i++;  i++;
        if(hid_callback_data != NULL){
            unsigned char * buffer = hid_callback_data->buf;
            if(buffer != NULL){
                HIDDevice * p_device = hid_callback_data->device;

                PRINT_TEXT2(0, i, "vid              %04x\n", SWAP16(p_device->vid)); i++;
                PRINT_TEXT2(0, i, "pid              %04x\n", SWAP16(p_device->pid)); i++;
                PRINT_TEXT2(0, i, "interface index  %02x\n", p_device->interface_index);i++;
                PRINT_TEXT2(0, i, "sub class        %02x\n", p_device->sub_class);i++;
                PRINT_TEXT2(0, i, "protocol         %02x\n", p_device->protocol);i++;
                PRINT_TEXT2(0, i, "max packet in    %02x\n", p_device->max_packet_size_rx);i++;
                PRINT_TEXT2(0, i, "max packet out   %02x\n", p_device->max_packet_size_tx);i++;

                PRINT_TEXT2(0, i, "bytes transfered: %d", hid_callback_data->transfersize); i++;
                i++;
                PRINT_TEXT1(0, i, "Pos: 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15"); i++;
                PRINT_TEXT1(0, i, "----------------------------------------------------"); i++;
                PRINT_TEXT2(0, i, "     %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7], buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15]); i++;

            }
        }
        PRINT_TEXT1(0, 17, "Press HOME to return to the Homebrew Launcher");
        if(vpad_data.btns_h & VPAD_BUTTON_HOME){
            break;
        }

    }while(1);

    OSScreenFlipBuffersEx(0);
    OSScreenFlipBuffersEx(1);

    free(msg);
}
