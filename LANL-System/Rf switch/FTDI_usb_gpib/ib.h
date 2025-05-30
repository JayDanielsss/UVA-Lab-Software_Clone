/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/


enum sad_special_address
{
	NO_SAD = 0,
	ALL_SAD = -1
};

enum send_eotmode
{
	NULLend = 0,
	DABend = 1,
	NLend = 2
};

#ifdef USB_GPIB_RENAME
// this kluge is used since LabView RT includes all these functions already
#define AllSPoll AllSPoll_USB
#define AllSpoll AllSpoll_USB
#define DevClear DevClear_USB
#define DevClearList DevClearList_USB
#define EnableLocal EnableLocal_USB
#define EnableRemote EnableRemote_USB
#define FindLstn FindLstn_USB
#define FindRQS FindRQS_USB
#define PassControl PassControl_USB
#define PPoll PPoll_USB
#define PPollConfig PPollConfig_USB
#define PPollUnconfig PPollUnconfig_USB
#define RcvRespMsg RcvRespMsg_USB
#define ReadStatusByte ReadStatusByte_USB
#define Receive Receive_USB
#define ReceiveSetup ReceiveSetup_USB
#define ResetSys ResetSys_USB
#define Send Send_USB
#define SendCmds SendCmds_USB
#define SendDataBytes SendDataBytes_USB
#define SendIFC SendIFC_USB
#define SendLLO SendLLO_USB
#define SendList SendList_USB
#define SendSetup SendSetup_USB
#define SetRWLS SetRWLS_USB
#define TestSRQ TestSRQ_USB
#define TestSys TestSys_USB
#define ThreadIbsta ThreadIbsta_USB
#define ThreadIberr ThreadIberr_USB
#define ThreadIbcnt ThreadIbcnt_USB
#define ThreadIbcntl ThreadIbcntl_USB
#define Trigger Trigger_USB
#define TriggerList TriggerList_USB
#define WaitSRQ WaitSRQ_USB
#define ibask ibask_USB
#define ibbna ibbna_USB
#define ibcac ibcac_USB
#define ibclr ibclr_USB
#define ibcmd ibcmd_USB
#define ibcmda ibcmda_USB
#define ibconfig ibconfig_USB
#define ibdev ibdev_USB
#define ibdma ibdma_USB
#define ibeot ibeot_USB
#define ibeos ibeos_USB
#define ibevent ibevent_USB
#define ibfind ibfind_USB
#define ibgt ibgt_USB
#define ibist ibist_USB
#define iblines iblines_USB
#define ibln ibln_USB
#define ibloc ibloc_USB
#define ibonl ibonl_USB
#define ibpad ibpad_USB
#define ibpct ibpct_USB
#define ibppc ibppc_USB
#define ibrd ibrd_USB
#define ibrda ibrda_USB
#define ibrdf ibrdf_USB
#define ibrpp ibrpp_USB
#define ibrsc ibrsc_USB
#define ibrsp ibrsp_USB
#define ibrsv ibrsv_USB
#define ibsad ibsad_USB
#define ibsic ibsic_USB
#define ibsre ibsre_USB
#define ibstop ibstop_USB
#define ibtmo ibtmo_USB
#define ibtrg ibtrg_USB
#define ibwait ibwait_USB
#define ibwrt ibwrt_USB
#define ibwrta ibwrta_USB
#define ibwrtf ibwrtf_USB

#define ibsta ibsta_USB
#define ibcnt ibcnt_USB
#define iberr iberr_USB
#define ibcntl ibcntl_USB

#endif

#include <stdint.h>

#define T500ms 0
#define T1s 1
#define T2s 2
#define T3s 3

typedef uint16_t Addr4882_t;
static const Addr4882_t NOADDR = (Addr4882_t)-1;

volatile int ibsta, ibcnt, iberr;
volatile long ibcntl;

#define MAX_BIOS 1024
volatile char bios_ver[MAX_BIOS];

extern void AllSPoll( int board_desc, const Addr4882_t addressList[], short resultList[] );
extern void AllSpoll( int board_desc, const Addr4882_t addressList[], short resultList[] );
extern void DevClear( int board_desc, Addr4882_t address );
extern void DevClearList( int board_desc, const Addr4882_t addressList[] );
extern void EnableLocal( int board_desc, const Addr4882_t addressList[] );
extern void EnableRemote( int board_desc, const Addr4882_t addressList[] );
extern void FindLstn( int board_desc, const Addr4882_t padList[],
	Addr4882_t resultList[], int maxNumResults );
extern void FindRQS( int board_desc, const Addr4882_t addressList[], short *result );
extern void PassControl( int board_desc, Addr4882_t address );
extern void PPoll( int board_desc, short *result );
extern void PPollConfig( int board_desc, Addr4882_t address, int dataLine, int lineSense );
extern void PPollUnconfig( int board_desc, const Addr4882_t addressList[] );
extern void RcvRespMsg( int board_desc, void *buffer, long count, int termination );
extern void ReadStatusByte( int board_desc, Addr4882_t address, short *result );
extern void Receive( int board_desc, Addr4882_t address,
	void *buffer, long count, int termination );
extern void ReceiveSetup( int board_desc, Addr4882_t address );
extern void ResetSys( int board_desc, const Addr4882_t addressList[] );
extern void Send( int board_desc, Addr4882_t address, const void *buffer,
	long count, int eot_mode );
extern void SendCmds( int board_desc, const void *cmds, long count );
extern void SendDataBytes( int board_desc, const void *buffer,
	long count, int eotmode );
extern void SendIFC( int board_desc );
extern void SendLLO( int board_desc );
extern void SendList( int board_desc, const Addr4882_t addressList[], const void *buffer,
	long count, int eotmode );
extern void SendSetup( int board_desc, const Addr4882_t addressList[] );
extern void SetRWLS( int board_desc, const Addr4882_t addressList[] );
extern void TestSRQ( int board_desc, short *result );
extern void TestSys( int board_desc, const Addr4882_t addressList[],
	short resultList[] );
extern int ThreadIbsta( void );
extern int ThreadIberr( void );
extern int ThreadIbcnt( void );
extern long ThreadIbcntl( void );
extern void Trigger( int board_desc, Addr4882_t address );
extern void TriggerList( int board_desc, const Addr4882_t addressList[] );
extern void WaitSRQ( int board_desc, short *result );
extern int ibask( int ud, int option, int *value );
extern int ibbna( int ud, char *board_name );
extern int ibcac( int ud, int synchronous );
extern int ibclr( int ud );
extern int ibcmd( int ud, const void *cmd, long cnt );
extern int ibcmda( int ud, const void *cmd, long cnt );
extern int ibconfig( int ud, int option, int value );
extern int ibdev( int board_index, int pad, int sad, int timo, int send_eoi, int eosmode );
extern int ibdma( int ud, int v );
extern int ibeot( int ud, int v );
extern int ibeos( int ud, int v );
extern int ibevent( int ud, short *event );
extern int ibfind( const char *dev );
extern int ibgts(int ud, int shadow_handshake);
extern int ibist( int ud, int ist );
extern int iblines( int ud, short *line_status );
extern int ibln( int ud, int pad, int sad, short *found_listener );
extern int ibloc( int ud );
extern int ibonl( int ud, int onl );
extern int ibpad( int ud, int v );
extern int ibpct( int ud );
extern int ibppc( int ud, int v );
extern int ibrd( int ud, void *buf, long count );
extern int ibrda( int ud, void *buf, long count );
extern int ibrdf( int ud, const char *file_path );
extern int ibrpp( int ud, char *ppr );
extern int ibrsc( int ud, int v );
extern int ibrsp( int ud, char *spr );
extern int ibrsv( int ud, int v );
extern int ibsad( int ud, int v );
extern int ibsic( int ud );
extern int ibsre( int ud, int v );
extern int ibstop( int ud );
extern int ibtmo( int ud, int v );
extern int ibtrg( int ud );
extern int ibwait( int ud, int mask );
extern int ibwrt( int ud, const void *buf, long count );
extern int ibwrta( int ud, const void *buf, long count );
extern int ibwrtf( int ud, const char *file_path ); 

#define DCAS  0x1
#define DTAS  0x2
#define LACS  0x4
#define TACS  0x8
#define ATN  0x10
#define CIC  0x20
#define REM  0x40
#define LOK  0x80
#define CMPL  0x100
#define EVENT  0x200
#define SPOLL  0x400
#define RQS  0x800
#define SRQI  0x1000
#define END  0x2000
#define TIMO  0x4000
#define ERR  0x8000

#define EDVR  0  /* A system call has failed. ibcnt/ibcntl will be set to the value of errno.*/
#define ECIC  1  /* Your interface board needs to be controller-in-charge, but is not.*/
#define ENOL  2  /* You have attempted to write data or command bytes, but there are no listeners currently addressed.*/
#define EADR  3  /* The interface board has failed to address itself properly before starting an io operation.*/
#define EARG  4  /* One or more arguments to the function call were invalid.*/
#define ESAC  5  /* The interface board needs to be system controller, but is not. */
#define EABO  6  /* A read or write of data bytes has been aborted, possibly due to a timeout or reception of a device clear command.*/
#define ENEB  7  /* The GPIB interface board does not exist, its driver is not loaded, or it is not configured properly.*/
#define EDMA  8  /* Not used (DMA error), included for compatibility purposes. */
#define EOIP  10  /* Function call can not proceed due to an asynchronous IO operation (ibrda, ibwrta, or ibcmda) in progress. */
#define ECAP  11  /* Incapable of executing function call, due the GPIB board lacking the capability, or the capability being disabled in software. */
#define EFSO  12  /* File system error. ibcnt/ibcntl will be set to the value of errno. */
#define EBUS  14  /* An attempt to write command bytes to the bus has timed out. */
#define ESTB  15  /* One or more serial poll status bytes have been lost. This can occur due to too many status bytes accumulating (through automatic serial polling) without being read. */
#define ESRQ  16  /* The serial poll request service line is stuck on. This can occur if a physical device on the bus requests service, but its GPIB address has not been opened (via ibdev for example) by any process. Thus the automatic serial polling routines are unaware of the device's existence and will never serial poll it.  */
#define ETAB  20  /* This error can be returned by ibevent, FindLstn, or FindRQS. See their descriptions for more information. */

#define REOS  0x400  /* Enable termination of reads when eos character is received.*/
#define XEOS  0x800  /* Assert the EOI line whenever the eos character is sent during writes.*/
#define BIN  0x1000  /* Match eos character using all 8 bits (instead of only looking at the 7 least significant bits).*/
