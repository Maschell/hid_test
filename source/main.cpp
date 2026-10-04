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

#include <coreinit/cache.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/memheap.h>
#include <coreinit/screen.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <nsyshid/hid.h>
#include <proc_ui/procui.h>
#include <sndcore2/core.h>
#include <sysapp/launch.h>
#include <vpad/input.h>
#include <whb/log.h>
#include <whb/log_udp.h>
#include <whb/proc.h>

#define SWAP16(x) ((uint16_t) (((x) >> 8) | (((x) &0xFF) << 8)))

#define MAX_DEVICES     8
#define MAX_REPORT_SIZE 1024
#define SHOWN_BYTES     64

// One entry per attached HID interface. The HID read callbacks write into
// these from a different thread than the render loop; a torn read only
// means one frame shows a half-updated report, which is fine for a viewer.
typedef struct {
    volatile bool active;
    uint32_t handle;
    uint16_t vid;
    uint16_t pid;
    uint8_t interfaceIndex;
    uint8_t subClass;
    uint8_t protocol;
    uint16_t maxPacketSizeRx;
    uint16_t maxPacketSizeTx;
    volatile uint32_t reportCount;
    volatile uint32_t reportSize;
    uint8_t report[MAX_REPORT_SIZE];
    uint8_t baseline[MAX_REPORT_SIZE];
    bool baselineSet;
} DeviceSlot;

static HIDClient gHIDClient;
static void *gTVBuffer;
static void *gDRCBuffer;
static uint32_t gTVSize;
static uint32_t gDRCSize;
static volatile bool gHasForeground;
static bool gExitRequested;

#define SCREEN_FRAME_HEAP_TAG 0x48494454 // 'HIDT'
static DeviceSlot gDevices[MAX_DEVICES];
static uint8_t gReadBuffers[MAX_DEVICES][MAX_REPORT_SIZE] __attribute__((aligned(64)));

static void readCallback(uint32_t handle, int32_t error, uint8_t *buffer, uint32_t bytesTransferred, void *userContext) {
    DeviceSlot *slot = (DeviceSlot *) userContext;
    if (error != 0 || slot == NULL || !slot->active || slot->handle != handle) {
        return; // device detached, don't re-arm
    }
    if (bytesTransferred > MAX_REPORT_SIZE) {
        bytesTransferred = MAX_REPORT_SIZE;
    }
    if (bytesTransferred != slot->reportSize || memcmp(slot->report, buffer, bytesTransferred) != 0) {
        char line[3 * 32 + 1] = {};
        for (uint32_t i = 0; i < bytesTransferred && i < 32; i++) {
            snprintf(&line[i * 3], 4, "%02X ", buffer[i]);
        }
        WHBLogPrintf("%04x:%04x %s", slot->vid, slot->pid, line);
    }
    memcpy(slot->report, buffer, bytesTransferred);
    slot->reportSize = bytesTransferred;
    slot->reportCount = slot->reportCount + 1;

    HIDRead(handle, buffer, slot->maxPacketSizeRx, readCallback, slot);
}

static int32_t attachCallback(HIDClient *client, HIDDevice *device, HIDAttachEvent attach) {
    if (attach == HID_DEVICE_DETACH) {
        for (int i = 0; i < MAX_DEVICES; i++) {
            if (gDevices[i].active && gDevices[i].handle == device->handle) {
                WHBLogPrintf("%04x:%04x detached", gDevices[i].vid, gDevices[i].pid);
                gDevices[i].active = false;
            }
        }
        return HID_DEVICE_DETACH;
    }

    for (int i = 0; i < MAX_DEVICES; i++) {
        DeviceSlot *slot = &gDevices[i];
        if (slot->active) {
            continue;
        }
        slot->handle          = device->handle;
        slot->vid             = SWAP16(device->vid);
        slot->pid             = SWAP16(device->pid);
        slot->interfaceIndex  = device->interfaceIndex;
        slot->subClass        = device->subClass;
        slot->protocol        = device->protocol;
        slot->maxPacketSizeRx = device->maxPacketSizeRx > MAX_REPORT_SIZE ? MAX_REPORT_SIZE : device->maxPacketSizeRx;
        slot->maxPacketSizeTx = device->maxPacketSizeTx;
        slot->reportCount     = 0;
        slot->reportSize      = 0;
        slot->baselineSet     = false;
        slot->active          = true;

        WHBLogPrintf("%04x:%04x attached (interface %d, max packet in %d)", slot->vid, slot->pid, slot->interfaceIndex, slot->maxPacketSizeRx);

        // The GC adapter only starts sending reports after this init command.
        if (slot->vid == 0x057e && slot->pid == 0x0337) {
            gReadBuffers[i][0] = 0x13;
            HIDWrite(device->handle, gReadBuffers[i], 1, NULL, NULL);
        }

        HIDRead(device->handle, gReadBuffers[i], slot->maxPacketSizeRx, readCallback, slot);
        return HID_DEVICE_ATTACH;
    }

    WHBLogPrintf("No free slot for %04x:%04x", SWAP16(device->vid), SWAP16(device->pid));
    return HID_DEVICE_DETACH;
}

static void print(int row, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void print(int row, const char *fmt, ...) {
    char msg[128];
    va_list va;
    va_start(va, fmt);
    vsnprintf(msg, sizeof(msg), fmt, va);
    va_end(va);
    OSScreenPutFontEx(SCREEN_TV, 0, row, msg);
    OSScreenPutFontEx(SCREEN_DRC, 0, row, msg);
}

// Collects the next active slot in the given direction, starting from current.
static int nextActive(int current, int step) {
    for (int n = 1; n <= MAX_DEVICES; n++) {
        int i = (current + step * n + MAX_DEVICES * n) % MAX_DEVICES;
        if (gDevices[i].active) {
            return i;
        }
    }
    return current;
}

static void drawDevice(DeviceSlot *slot, int index, int activeCount, int row) {
    uint8_t report[MAX_REPORT_SIZE];
    uint32_t size = slot->reportSize;
    memcpy(report, slot->report, size);

    print(row++, "Device %d of %d   vid %04x  pid %04x", index, activeCount, slot->vid, slot->pid);
    print(row++, "interface %02x  subclass %02x  protocol %02x", slot->interfaceIndex, slot->subClass, slot->protocol);
    print(row++, "max packet in %d  out %d", slot->maxPacketSizeRx, slot->maxPacketSizeTx);
    print(row++, "reports %u  last length %u", slot->reportCount, size);
    row++;

    print(row++, "Pos: 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F");
    print(row++, "---------------------------------------------------");
    uint32_t shown = size > SHOWN_BYTES ? SHOWN_BYTES : size;
    for (uint32_t base = 0; base < shown; base += 16) {
        char line[80];
        int len = snprintf(line, sizeof(line), " %02X:", base);
        for (uint32_t i = base; i < base + 16 && i < shown; i++) {
            len += snprintf(&line[len], sizeof(line) - len, " %02X", report[i]);
        }
        print(row++, "%s", line);
    }
    row++;

    // Bits that differ from the baseline, in controller_patcher INI order
    // (byte, mask), so a held button can be copied straight into a config.
    if (!slot->baselineSet) {
        print(row++, "Press A with nothing held to capture a baseline");
        return;
    }
    print(row++, "Changed vs baseline (byte, mask = now):");
    char line[80] = {};
    int len       = 0;
    int lines     = 0;
    for (uint32_t i = 0; i < size && lines < 2; i++) {
        uint8_t diff = report[i] ^ slot->baseline[i];
        if (!diff) {
            continue;
        }
        if (len > 40) {
            print(row++, "%s", line);
            lines++;
            len = 0;
        }
        len += snprintf(&line[len], sizeof(line) - len, " 0x%02X,0x%02X=%02X", i, diff, report[i]);
    }
    if (len > 0 && lines < 2) {
        print(row++, "%s", line);
    } else if (len == 0 && lines == 0) {
        print(row++, " (none)");
    }
}

// Screen buffers live in the MEM1 foreground heap, which the system takes back
// whenever we lose the foreground (HOME menu, exit). So, like wut's own
// WHBLogConsole, allocate them on acquire, free them on release, and never
// touch OSScreen while in the background.
//
// OSScreenSetBufferEx also resets the double-buffer state, so it must only run
// here and never per frame - otherwise we draw into the buffer being displayed.
static uint32_t acquireCallback(void *context) {
    MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
    MEMRecordStateForFrmHeap(heap, SCREEN_FRAME_HEAP_TAG);
    gTVBuffer  = MEMAllocFromFrmHeapEx(heap, gTVSize, 0x100);
    gDRCBuffer = MEMAllocFromFrmHeapEx(heap, gDRCSize, 0x100);

    OSScreenSetBufferEx(SCREEN_TV, gTVBuffer);
    OSScreenSetBufferEx(SCREEN_DRC, gDRCBuffer);
    OSScreenEnableEx(SCREEN_TV, TRUE);
    OSScreenEnableEx(SCREEN_DRC, TRUE);
    gHasForeground = true;
    return 0;
}

static uint32_t releaseCallback(void *context) {
    gHasForeground = false;
    MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
    MEMFreeByStateToFrmHeap(heap, SCREEN_FRAME_HEAP_TAG);
    return 0;
}

int main(int argc, char **argv) {
    WHBProcInit();
    WHBLogUdpInit();
    WHBLogPrintf("HID-TEST by Maschell, Aroma port. Built %s %s", __DATE__, __TIME__);

    // The system keeps playing the title loading sound until the app takes
    // over audio, so initialise AX even though we never play anything.
    AXInit();

    OSScreenInit();
    gTVSize  = OSScreenGetBufferSizeEx(SCREEN_TV);
    gDRCSize = OSScreenGetBufferSizeEx(SCREEN_DRC);
    acquireCallback(NULL);
    ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, acquireCallback, NULL, 100);
    ProcUIRegisterCallback(PROCUI_CALLBACK_RELEASE, releaseCallback, NULL, 100);

    HIDSetup();
    HIDAddClient(&gHIDClient, attachCallback);

    int selected = 0;
    while (WHBProcIsRunning()) {
        VPADStatus vpad;
        VPADReadError vpadError;
        uint32_t pressed = 0;
        if (VPADRead(VPAD_CHAN_0, &vpad, 1, &vpadError) > 0 && vpadError == VPAD_READ_SUCCESS) {
            pressed = vpad.trigger;
        }

        if (!gDevices[selected].active) {
            selected = nextActive(selected, 1);
        }
        if (pressed & VPAD_BUTTON_RIGHT) {
            selected = nextActive(selected, 1);
        }
        if (pressed & VPAD_BUTTON_LEFT) {
            selected = nextActive(selected, -1);
        }
        if ((pressed & VPAD_BUTTON_A) && gDevices[selected].active) {
            memcpy(gDevices[selected].baseline, gDevices[selected].report, MAX_REPORT_SIZE);
            gDevices[selected].baselineSet = true;
        }
        if ((pressed & VPAD_BUTTON_PLUS) && !gExitRequested) {
            WHBLogPrintf("Exit requested");
            gExitRequested = true;
            SYSLaunchMenu();
        }

        // WHBProcIsRunning returns once more after the foreground is released
        if (!gHasForeground || gExitRequested) {
            continue;
        }

        OSScreenClearBufferEx(SCREEN_TV, 0);
        OSScreenClearBufferEx(SCREEN_DRC, 0);

        print(0, "HID-TEST - by Maschell - Aroma port");

        int activeCount = 0;
        int index       = 0;
        for (int i = 0; i < MAX_DEVICES; i++) {
            if (gDevices[i].active) {
                activeCount++;
                if (i == selected) {
                    index = activeCount;
                }
            }
        }
        if (activeCount == 0) {
            print(2, "Attach a USB HID device...");
        } else {
            drawDevice(&gDevices[selected], index, activeCount, 2);
        }
        print(17, "Left/Right: device  A: baseline  +: exit");

        DCFlushRange(gTVBuffer, gTVSize);
        DCFlushRange(gDRCBuffer, gDRCSize);
        OSScreenFlipBuffersEx(SCREEN_TV);
        OSScreenFlipBuffersEx(SCREEN_DRC);

        // OSScreen has no vsync; there's no point redrawing faster than ~60Hz.
        OSSleepTicks(OSMillisecondsToTicks(16));
    }

    WHBLogPrintf("Main loop exited, shutting down");
    HIDDelClient(&gHIDClient);
    HIDTeardown();

    if (gHasForeground) {
        OSScreenShutdown();
        releaseCallback(NULL);
    }

    AXQuit();
    WHBLogPrintf("Shutdown complete");

    WHBLogUdpDeinit();
    WHBProcShutdown();
    return 0;
}
