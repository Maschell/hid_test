#ifndef COMMON_H
#define	COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "os_defs.h"

#ifndef MEM_BASE
#define MEM_BASE                    (0x00800000)
#endif


#define OS_FIRMWARE                 (*(volatile unsigned int*)(MEM_BASE + 0x1400 + 0x04))

#define OS_SPECIFICS                ((OsSpecifics*)(MEM_BASE + 0x1500))

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS                0
#endif
#define EXIT_HBL_EXIT               0xFFFFFFFE
#define EXIT_RELAUNCH_ON_LOAD       0xFFFFFFFD

#ifdef __cplusplus
}
#endif

#endif	/* COMMON_H */

