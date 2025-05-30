 
#include <ib.h>

//    FTDI USB to GPIB adapter driver
//    written by Danny Holstein

#ifdef WIN
 #define EXPORT __declspec(dllexport)
 #include <windows.h>
 typedef long ulong;
#else
 #define EXPORT
 #define _GNU_SOURCE
#endif

main()
{
(void) ibfind_USB("/dev/NULL");
}
