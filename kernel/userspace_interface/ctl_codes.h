#ifndef _CTL_CODES_
#define _CTL_CODES_

// The current CTL codes that should be used.

// #define IOCTL_SET_MODE   CTL_CODE(0x22, 0x101, 0, 3) 
#define IOCTL_SET_MODE    CTL_CODE(0x22, 0x101, 3, 3) 

// #define PMEM_WRITE_ENABLE CTL_CODE(0x22, 0x102, 0, 3)
#define IOCTL_WRITE_ENABLE  CTL_CODE(0x22, 0x102, 3, 3)

// #define IOCTL_GET_INFO  CTL_CODE(0x22, 0x103, 0, 3)
#define IOCTL_GET_INFO  CTL_CODE(0x22, 0x103, 3, 3)

/*
// REM :
#define METHOD_BUFFERED                 0
#define METHOD_IN_DIRECT                1
#define METHOD_OUT_DIRECT               2
#define METHOD_NEITHER                  3
*/

#endif
