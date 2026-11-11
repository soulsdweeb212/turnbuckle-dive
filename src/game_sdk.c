/* Stand-ins for the N64 SDK functions that the developer program's game files call and the port does not provide yet:
 * libultra's message queues, controllers, Rumble Pak, video, caches and threads, and the music library libmus. Each
 * one stops the program with its name, so a game file that reaches one says which function it needs. Each gets its
 * PC version (with the game's own types) when the developer program first runs the code that calls it; the
 * decompilation's include/port.h says why several are renamed port_... (their N64 records differ from LibUltraShip's).
 *
 * They are defined without parameters: a stand-in never reads its arguments, it stops first. */
#include "game.h"

#define TD_NOT_YET(name)                                                                                               \
    void name(void);                                                                                                   \
    void name(void) {                                                                                                  \
        port_fatal("the game called " #name ", which the port does not provide yet");                                  \
    }

/* Message queues (port.h: the game's queues keep the N64's 0x18 bytes and 4-byte messages). */
TD_NOT_YET(port_osCreateMesgQueue)
TD_NOT_YET(port_osSendMesg)
TD_NOT_YET(port_osRecvMesg)
TD_NOT_YET(port_osSetEventMesg)

/* Controllers and the Rumble Pak (port.h: the game's pad buffers keep the N64's sizes). */
TD_NOT_YET(port_osContInit)
TD_NOT_YET(port_osContStartQuery)
TD_NOT_YET(port_osContGetQuery)
TD_NOT_YET(port_osContStartReadData)
TD_NOT_YET(port_osContGetReadData)
TD_NOT_YET(port_osContSetCh)
TD_NOT_YET(port_osMotorInit)
TD_NOT_YET(port_osMotorAccess)

/* Cartridge reads: the animation streams' requests (PortIoMesg in port.h). */
TD_NOT_YET(port_osEPiStartDma)
TD_NOT_YET(osCartRomInit)

/* Video, the scheduler, threads, interrupts and caches. */
TD_NOT_YET(osViSetMode)
TD_NOT_YET(osViSetSpecialFeatures)
TD_NOT_YET(osViSetYScale)
TD_NOT_YET(osViBlack)
TD_NOT_YET(osScAddClient)
TD_NOT_YET(osCreateThread)
TD_NOT_YET(osStartThread)
TD_NOT_YET(osSetIntMask)
TD_NOT_YET(osAfterPreNMI)
TD_NOT_YET(osInvalDCache)
TD_NOT_YET(osWritebackDCache)
TD_NOT_YET(osWritebackDCacheAll)

/* Music and sound effects (libmus). */
TD_NOT_YET(MusStartSong)
TD_NOT_YET(MusStop)
TD_NOT_YET(MusAsk)
TD_NOT_YET(MusSetMasterVolume)
TD_NOT_YET(MusBankStartEffect)
TD_NOT_YET(MusBankStartEffect2)
TD_NOT_YET(MusHandleAsk)
TD_NOT_YET(MusHandleStop)
TD_NOT_YET(MusHandleSetReverb)
