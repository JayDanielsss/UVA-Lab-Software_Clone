#include "ib.h"

//    FTDI USB to GPIB adapter driver
//    written by Danny Holstein

#ifdef WIN
 #define EXPORT __declspec(dllexport)
 #include <windows.h>
 typedef long ulong;
#else
 #define EXPORT
 #define _GNU_SOURCE
 #define USB_POLL
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <poll.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

int GPIB_open_socket(char *IP_address, int port);

fd_set rfds; struct timeval tv, tv_zero = {0,0}; int retval;

#define PORT_CHECK(port_hdl) {if (port_hdl == -1)\
 {sprintf(FTDI_error, "%s\n", strerror( errno )); ibsta = ERR; iberr = EFSO; ibcnt = errno; return(ibsta);}}
#define TCP_CHECK(port, error) {if (port == -1)\
 {sprintf(FTDI_error, "%s\n", error); ibsta = ERR; iberr = EFSO; ibcnt = errno; return(ibsta);}}
#define RTN_ERROR {sprintf(FTDI_error, "%s\n", strerror( errno )); ibsta = ERR; iberr = EFSO; ibcnt = errno; return(ibsta);}
#define RTN_ERROR_V {sprintf(FTDI_error, "%s\n", strerror( errno )); ibsta = ERR; iberr = EFSO; ibcnt = errno; return;}
#define TIMO_ERROR(a) {sprintf(FTDI_error, "Timeout error on GPIB\nAddress: %2d\n", (a));\
		ibsta = ERR | TIMO; iberr = EABO; ibcnt = 0; return(ibsta);}
#define TIMO_ERROR_V(a) {sprintf(FTDI_error, "Timeout error on GPIB\nAddress: %2d\n", (a));\
		ibsta = ERR | TIMO; iberr = EABO; ibcnt = 0; return;}
#define CHK_INIT(ud) if (FTDI_dev[ud].used == 0)\
	{ibsta = ERR; iberr = EARG; ibcnt = 0; strcpy(FTDI_error, "Uninitialized device"); return(ibsta);}\
	else {ibsta = 0; iberr = 0; ibcnt = 0; strcpy(FTDI_error, "");}
#define CHK_INIT_V(ud) if (FTDI_dev[ud].used == 0)\
	{ibsta = ERR; iberr = EARG; ibcnt = 0; strcpy(FTDI_error, "Uninitialized device"); return;}\
	else {ibsta = 0; iberr = 0; ibcnt = 0; strcpy(FTDI_error, "");}

int timo[]     = {500, 1000, 2000, 3000},
    timo_ser[] = {550, 1200, 2300, 3300};
#define S_TV(a) timo_ser[a]/1000
#define MS_TV(a) timo_ser[a] % 1000

#define MAX(a,b) (( a > b ) ? a : b )
#define MIN(a,b) (( a < b ) ? a : b )

#define MAX_DEVS 32

volatile int last_pad = 0;
typedef struct  {
	char used, controller, TCP, termination[3];
	int port, pad, sad;
	int timo, send_eoi, eosmode;
} FTDI_DEV;
static FTDI_DEV FTDI_dev[MAX_DEVS] = {0, 0, 0, 0, 0, 0, 0, 0};

char FTDI_error[1024], cmd[1024];
FILE *dev_file;

EXPORT void AllSPoll( int board_desc, const Addr4882_t addressList[], short resultList[])
{
char buf[8], dummy[8]; int i=0;
CHK_INIT_V(board_desc);
while (addressList[i] != NOADDR) {
	sprintf(cmd,"++spoll %d\n", addressList[i]);
	write(FTDI_dev[board_desc].port, cmd, strlen(cmd));
	
	/* Watch port to see when it has input. */
	FD_ZERO(&rfds);FD_SET(FTDI_dev[board_desc].port, &rfds); tv.tv_sec = 1; tv.tv_usec = 0;
	retval = select(FTDI_dev[board_desc].port+1, &rfds, NULL, NULL, &tv);
	if (retval == -1)
		RTN_ERROR_V
	else if (retval)
		{if (read(FTDI_dev[board_desc].port, buf, 8) == -1) RTN_ERROR_V
		while (select(FTDI_dev[board_desc].port+1, &rfds, NULL, NULL, &tv_zero)>0)
			{if ((read(FTDI_dev[board_desc].port, dummy, 8)) == -1) RTN_ERROR_V;}}
	else
		TIMO_ERROR_V(FTDI_dev[board_desc].pad)
	sscanf(buf, "%d", &resultList[i]); i++;
	}
}

EXPORT void FindRQS( int board_desc, const Addr4882_t addressList[], short *result )
{
int i=0, len=0, j=0;
short resultList[MAX_DEVS];
*result = 0;
CHK_INIT_V(board_desc);

while (addressList[len] != NOADDR) {len++;}

while (!(*result & 0x40) && j<1000) {
	AllSPoll(board_desc, addressList, resultList);
	for (i=0; i<len; i++) {
		if (resultList[i] & 0x40) {*result = resultList[i]; ibcnt = i; break;}
		}
	}
	usleep(200000); j++;
}

EXPORT void TestSRQ( int board_index, short *result )
{
  int i, k=0, count=8;
  char buf[8], dummy[8], *line_buf;
CHK_INIT_V(board_index);

sprintf(cmd,"++srq\n");
if (FTDI_dev[board_index].TCP) {
  printf("TestSRQ: cmd=%s\n", cmd);
  if (send(FTDI_dev[board_index].port, cmd, strlen(cmd), 0) != strlen(cmd)) TCP_CHECK(-1, "Data transmission error (TCP/GPIB)");}
else {write(FTDI_dev[board_index].port, cmd, strlen(cmd));}

  struct pollfd fds;
  fds.fd = FTDI_dev[board_index].port; fds.events = POLLIN;
  switch (poll(&fds, 1, timo[FTDI_dev[board_index].timo])) {
    case -1 :	// error
	    RTN_ERROR
	    break;
    case  0 :	//  timeout
	    TIMO_ERROR(FTDI_dev[board_index].pad)
	    break;
    default:	//  OK to read
	    line_buf = (char*) buf;
	    fcntl(FTDI_dev[board_index].port, F_SETFL, FNDELAY);
	    if (FTDI_dev[board_index].TCP) {
	      while ((i=recv(FTDI_dev[board_index].port, line_buf+k, count-k, 0))>0) {
		      k += i; usleep(100000);}}
	    else {
	      while ((i=read(FTDI_dev[board_index].port, line_buf+k, count-k))>0) {
		      k += i; usleep(100000);}}
	    fcntl(FTDI_dev[board_index].port, F_SETFL, 0);
	    line_buf[k] = NULL;
	    break;
}

  printf("TestSRQ: buf=%s\n", buf);
*result = 0;
if (*buf == '1') *result = 1;
ibsta = CMPL; ibcnt = 0;
}

EXPORT void Trigger( int board_index, Addr4882_t address )
{
CHK_INIT_V(board_index);
sprintf(cmd,"++addr %d\n++trg\n", address); write(FTDI_dev[board_index].port, cmd, strlen(cmd));
ibsta = CMPL; ibcnt = 0;
}

EXPORT int ibclr( int ud )
{
  CHK_INIT(ud);
  if (last_pad != FTDI_dev[ud].pad) sprintf(cmd,"++addr %d\n++clr\n", FTDI_dev[ud].pad);
  else sprintf(cmd,"++clr\n");

  if (FTDI_dev[ud].TCP) {
    if (send(FTDI_dev[ud].port, cmd, strlen(cmd), 0) != strlen(cmd)) TCP_CHECK(-1, "ibclr: Data transmission error (TCP/GPIB)");}
  else {
    write(FTDI_dev[ud].port, cmd, strlen(cmd));}

  ibsta |= CMPL; ibcnt = 0; last_pad = FTDI_dev[ud].pad;
  return(ibsta);
}

EXPORT int ibdev( int board_index, int pad, int sad, int timo, int send_eoi, int eosmode)
{
int i, j;
FTDI_DEV *dev;
if (FTDI_dev[board_index].used == 0) {strcpy(FTDI_error, "Illegal board_index"); ibsta=ERR; iberr=ENEB; return (-1);}

//	search for free device resources, assign device to it
for (j=0; j<MAX_DEVS; j++) {if (FTDI_dev[j].used==0) break;}
if (j>=MAX_DEVS) {sprintf(FTDI_error, "Max devices (%d), use ibonl() to free resources", MAX_DEVS); return(-1);}

dev = &FTDI_dev[j];
dev->used = 1;	dev->controller = 0; strcpy(dev->termination, "");dev->TCP = FTDI_dev[board_index].TCP;
dev->port = FTDI_dev[board_index].port;	dev->pad = pad;           dev->sad = sad;
dev->timo = MIN(timo, 3);		dev->send_eoi = send_eoi; dev->eosmode = eosmode;

return(j);
}

EXPORT int ibeos( int ud, int v )
{
CHK_INIT(ud);
if (last_pad != FTDI_dev[ud].pad) {
	sprintf(cmd,"++addr %d\n", FTDI_dev[ud].pad); write(FTDI_dev[ud].port, cmd, strlen(cmd));}

if (REOS && v) {sprintf(cmd,"++eot_enable 1\n");}
else {sprintf(cmd,"++eot_enable 0\n");}
	write(FTDI_dev[ud].port, cmd, strlen(cmd));

if (XEOS && v) {sprintf(cmd,"++eoi 1\n");}
else {sprintf(cmd,"++eoi 0\n");}
	write(FTDI_dev[ud].port, cmd, strlen(cmd));

if (0x00FF && v)  {sprintf(cmd,"++eot_char %d\n", 0x00FF && v); write(FTDI_dev[ud].port, cmd, strlen(cmd));}
ibsta |= CMPL; ibcnt = 0; last_pad = FTDI_dev[ud].pad;
return(ibsta);
}

EXPORT int ibeot( int ud, int v )
{
char i=0;
if (v) i = 1;
CHK_INIT(ud);

if (last_pad != FTDI_dev[ud].pad) sprintf(cmd,"++addr %d\n++eoi %d\n", FTDI_dev[ud].pad, i);
else sprintf(cmd,"++eoi %d\n", i);

if (FTDI_dev[ud].TCP) {
  if (send(FTDI_dev[ud].port, cmd, strlen(cmd), 0) != strlen(cmd)) TCP_CHECK(-1, "ibeot: Data transmission error (TCP/GPIB)");}
else {
  write(FTDI_dev[ud].port, cmd, strlen(cmd));}
  
ibsta |= CMPL; ibcnt = 0; last_pad = FTDI_dev[ud].pad;
return(ibsta);
}

EXPORT int ibfind( const char *dev )
{
int port, i, j, k=0;
char servIP[128], TCP=0;
if (sscanf(dev, "%[^:]:%d", servIP, &port) == 2) {
  port = GPIB_open_socket(servIP, port); TCP=1;
  TCP_CHECK(port, "Connect failure");}
else {
  port = open(dev,O_RDWR|O_NOCTTY);
  PORT_CHECK(port);}

sprintf(cmd,"\n++ifc\n++ver\n");
if (TCP) {
    /* Send the string to the TCP/GPIB "server" */
    if (send(port, cmd, strlen(cmd), 0) != strlen(cmd)) TCP_CHECK(-1, "Data transmission error");
    if ((k = recv(port, bios_ver, MAX_BIOS - 1, 0)) <= 0) TCP_CHECK(-1, "Data receive error");
  }
else {
  write(port, cmd, strlen(cmd));
  tcdrain(port); usleep(500000);

  /* Watch port to see when it has input. */
  FD_ZERO(&rfds);FD_SET(port, &rfds); tv.tv_sec = 0; tv.tv_usec = 1000;
  retval = select(port+1, &rfds, NULL, NULL, &tv);
      if (retval == -1)
	RTN_ERROR
      else if (retval)
	  {while (select(port+1, &rfds, NULL, NULL, &tv_zero)>0)
		  {if ((read(port, bios_ver+(k++), 1)) == -1) RTN_ERROR; if (k >= 1024) break;}
		  *(bios_ver+k) = NULL; /*printf("ibfind: %s\n", bios_ver);*/}
      else
	  TIMO_ERROR(0) ;}

//	search for free device resources, assign board to it
for (j=0; j<MAX_DEVS; j++) {if (FTDI_dev[j].used==0) break;}
if (j>=MAX_DEVS) {sprintf(FTDI_error, "Max devices (%d), use ibonl() to free resources", MAX_DEVS); return(-1);}
FTDI_dev[j].used=1; FTDI_dev[j].controller=1; FTDI_dev[j].TCP=TCP;
FTDI_dev[j].port = port;	FTDI_dev[j].pad = 0;           FTDI_dev[j].sad = 0;
FTDI_dev[j].timo = 0;		FTDI_dev[j].send_eoi = 0; FTDI_dev[j].eosmode = 0;
ibsta = CIC;
return(j);
}

EXPORT int ibloc( int ud )
{
CHK_INIT(ud);
if (last_pad != FTDI_dev[ud].pad) sprintf(cmd,"++addr %d\n++loc\n", FTDI_dev[ud].pad);
else sprintf(cmd,"++loc\n");

if (FTDI_dev[ud].TCP) {
  if (send(FTDI_dev[ud].port, cmd, strlen(cmd), 0) != strlen(cmd)) TCP_CHECK(-1, "ibloc: Data transmission error (TCP/GPIB)");}
else {
  write(FTDI_dev[ud].port, cmd, strlen(cmd));}
write(FTDI_dev[ud].port, cmd, strlen(cmd));
ibsta |= CMPL; ibcnt = 0; last_pad = FTDI_dev[ud].pad;
return(ibsta);
}

EXPORT int ibonl( int ud, int onl )
{
int i, j;
FTDI_DEV *dev;
CHK_INIT(ud);

dev = &FTDI_dev[ud];

if (dev->controller==1) {
	if (onl) {}
	else {
		close(dev->port);
		dev->used = 0;	dev->controller = 0;
		dev->port = 0;	dev->pad = 0;	dev->sad = 0;
		dev->timo = 0;	dev->send_eoi = 0; dev->eosmode = 0;}
  }
else {
	if (onl) {}
	else {
		dev->used = 0;	dev->controller = 0;
		dev->port = 0;	dev->pad = 0;	dev->sad = 0;
		dev->timo = 0;	dev->send_eoi = 0; dev->eosmode = 0;}
  }
return (ibsta);
}

EXPORT int ibrd( int ud, void *buf, long count )
{
int i, j, k=0; char *line_buf;
CHK_INIT(ud);
if (count == 0) {ibsta |= CMPL; ibcnt = 0; return(ibsta);}
if (last_pad != FTDI_dev[ud].pad) {
  sprintf(cmd,"++addr %d\n++eot_char 4\n++eot_enable 1\n++read_tmo_ms %d\n",
	  FTDI_dev[ud].pad, timo[FTDI_dev[ud].timo]);
  if (FTDI_dev[ud].TCP) {
    printf("ibrd: cmd=%s\n", cmd);
    if (send(FTDI_dev[ud].port, cmd, strlen(cmd), 0) != strlen(cmd)) TCP_CHECK(-1, "Data transmission error (TCP/GPIB)");}
  else {write(FTDI_dev[ud].port, cmd, strlen(cmd));}}

sprintf(cmd,"++auto 1\n");
if (FTDI_dev[ud].TCP) {
  printf("ibrd: cmd=%s\n", cmd);
  if (send(FTDI_dev[ud].port, cmd, strlen(cmd), 0) != strlen(cmd)) TCP_CHECK(-1, "Data transmission error (TCP/GPIB)");}
else {
  if (write(FTDI_dev[ud].port, cmd, strlen(cmd)) <= 0) RTN_ERROR;
  tcdrain(FTDI_dev[ud].port);}

#ifdef USB_POLL
  struct pollfd fds;
  fds.fd = FTDI_dev[ud].port; fds.events = POLLIN;
  switch (poll(&fds, 1, timo[FTDI_dev[ud].timo])) {
    case -1 :	// error
	    RTN_ERROR
	    break;
    case  0 :	//  timeout
	    TIMO_ERROR(FTDI_dev[ud].pad)
	    break;
    default:	//  OK to read
	    line_buf = (char*) buf;
	    fcntl(FTDI_dev[ud].port, F_SETFL, FNDELAY);
	    if (FTDI_dev[ud].TCP) {
	      while ((i=recv(FTDI_dev[ud].port, line_buf+k, count-k, 0))>0) {
		      k += i; usleep(100000);}}
	    else {
	      while ((i=read(FTDI_dev[ud].port, line_buf+k, count-k))>0) {
		      k += i; usleep(100000);}}
	    fcntl(FTDI_dev[ud].port, F_SETFL, 0);
	    line_buf[k] = NULL;
	    break;
}

#else
if (FTDI_dev[ud].TCP) {
  line_buf = (char*) buf;
  if ((k = recv(FTDI_dev[ud].port, line_buf, count - 1, 0)) <= 0) TCP_CHECK(-1, "Data receive error");
//   printf("ibrd: line_buf=%s\n", line_buf);}
else {
  /* Watch port to see when it has input. */
  FD_ZERO(&rfds);FD_SET(FTDI_dev[ud].port, &rfds);
  tv.tv_sec = S_TV(FTDI_dev[ud].timo); tv.tv_usec = MS_TV(FTDI_dev[ud].timo);
  retval = select(FTDI_dev[ud].port+1, &rfds, NULL, NULL, &tv);
      if (retval == -1)
	  RTN_ERROR
      else if (retval)
	  {line_buf = (char*) buf;
	  dev_file = fdopen(FTDI_dev[ud].port, "r");
	  do {if ((fgets(line_buf+k, count-k, dev_file)) == -1) RTN_ERROR;
		  k += strlen(line_buf+k); /*tcflush(FTDI_dev[ud].port, TCIOFLUSH);*/}
	  while (select(FTDI_dev[ud].port+1, &rfds, NULL, NULL, &tv_zero)>0);
	  }
      else
	  TIMO_ERROR(FTDI_dev[ud].pad)
}
#endif

ibsta |= CMPL; ibcnt = k; last_pad = FTDI_dev[ud].pad;
return(ibsta);
}

EXPORT int ibrsc( int ud, int v )
{
CHK_INIT(ud);
if (!FTDI_dev[ud].controller) {ibsta = ERR; iberr = EARG; ibcnt = 0; last_pad = 0; return(ibsta);}
if (v) {sprintf(cmd,"++mode 1\n"); ibsta = CIC;}
else {sprintf(cmd,"++mode 0\n"); ibsta = 0;}

if (FTDI_dev[ud].TCP) {
  if (send(FTDI_dev[ud].port, cmd, strlen(cmd), 0) != strlen(cmd)) TCP_CHECK(-1, "ibrsc: Data transmission error (TCP/GPIB)");}
else {
  write(FTDI_dev[ud].port, cmd, strlen(cmd));}
  
ibsta |= CMPL; ibcnt = 0; last_pad = 0;
return(ibsta);
}

EXPORT int ibrsp( int ud, char *spr )
{
char buf[8], dummy[8]; int i;
CHK_INIT(ud);

if (!(FTDI_dev[ud].TCP)) {
  while (select(FTDI_dev[ud].port+1, &rfds, NULL, NULL, &tv_zero)>0)
	  {if ((i=read(FTDI_dev[ud].port, dummy, 8)) == -1) RTN_ERROR;}
}

if (last_pad != FTDI_dev[ud].pad) sprintf(cmd,"++spoll %d\n", FTDI_dev[ud].pad);
else sprintf(cmd,"++spoll\n");

if (FTDI_dev[ud].TCP) {
  if (send(FTDI_dev[ud].port, cmd, strlen(cmd), 0) != strlen(cmd)) TCP_CHECK(-1, "ibrsp: Data transmission error (TCP/GPIB)");}
else {
  write(FTDI_dev[ud].port, cmd, strlen(cmd));}

if (FTDI_dev[ud].TCP) {
  if (recv(FTDI_dev[ud].port, buf, 8, 0) <= 0) TCP_CHECK(-1, "ibrd: Data receive error");}
else {
  /* Watch port to see when it has input. */
  FD_ZERO(&rfds);FD_SET(FTDI_dev[ud].port, &rfds); tv.tv_sec = S_TV(FTDI_dev[ud].timo); tv.tv_usec = MS_TV(FTDI_dev[ud].timo);
  retval = select(FTDI_dev[ud].port+1, &rfds, NULL, NULL, &tv);
      if (retval == -1)
	  RTN_ERROR
      else if (retval)
	  {if (read(FTDI_dev[ud].port, buf, 8) == -1) RTN_ERROR
	  while (select(FTDI_dev[ud].port+1, &rfds, NULL, NULL, &tv_zero)>0)
		  {if ((read(FTDI_dev[ud].port, dummy, 8)) == -1) RTN_ERROR;}}
      else
	  TIMO_ERROR(FTDI_dev[ud].pad)
}
sscanf(buf, "%d", spr);
ibsta |= CMPL; ibcnt = 0; last_pad = FTDI_dev[ud].pad;
return(ibsta);
}

EXPORT int ibsic( int ud )
{
CHK_INIT(ud);
if (!FTDI_dev[ud].controller) {ibsta = ERR; iberr = EARG; ibcnt = 0; last_pad = 0; return(ibsta);}
sprintf(cmd,"++ifc\n"); write(FTDI_dev[ud].port, cmd, strlen(cmd));
ibsta = CMPL; ibcnt = 0; last_pad = 0;
return(ibsta);
}

EXPORT int ibsre( int ud, int enable )
{
CHK_INIT(ud);
if (!FTDI_dev[ud].controller) {ibsta = ERR; iberr = EARG; ibcnt = 0; last_pad = 0; return(ibsta);}
ibsta = CMPL; ibcnt = 0;
ibsta |= ERR; iberr = ECAP; last_pad = 0; return(ibsta);
}

EXPORT int ibtmo( int ud, int v )
{
CHK_INIT(ud);
FTDI_dev[ud].timo = v;

ibsta |= CMPL; ibcnt = 0; last_pad = 0;
return(ibsta);
}

EXPORT int ibwait( int ud, int mask )
{
CHK_INIT(ud);
ibsta = CMPL; ibcnt = 0;
if (mask) {ibsta |= ERR; iberr = ECAP; last_pad = 0; return(ibsta);}
return ibsta;
}

EXPORT int ibwrt( int ud, const void *buf, long count )
{
FTDI_DEV *dev;
CHK_INIT(ud);
char cmd[1024];

dev = &FTDI_dev[ud];
//   always set the address since it seems an intermediate read might bugger it

sprintf(cmd,"++addr %d\n++eot_char 4\n++eot_enable 1\n++read_tmo_ms %d\n++auto 0\n",
	FTDI_dev[ud].pad, timo[FTDI_dev[ud].timo]);

if (dev->TCP) {
    /* Send the string to the TCP/GPIB "server" */
    if (send(dev->port, cmd, strlen(cmd), 0) != strlen(cmd)) TCP_CHECK(-1, "Data transmission error (TCP/GPIB)");
    printf("ibwrt: cmd=%s, port=%d\n", cmd, dev->port);
    if (send(dev->port, buf, strlen(buf), 0) != strlen(buf)) TCP_CHECK(-1, "Data transmission error");
    printf("ibwrt: buf=%s, port=%d\n", buf, dev->port);
  }
else {
   write(dev->port, cmd, strlen(cmd));
   if (write(dev->port, buf, count) == -1) RTN_ERROR
}

ibsta |= CMPL; ibcnt = strlen(buf); last_pad = dev->pad;
// printf("ibwrt: %s\n", buf);
return(ibsta);
}

EXPORT char* FDTI_error_report() {return FTDI_error;}

EXPORT int FDTI_error_iberr() {return iberr;}

EXPORT int FDTI_error_ibsta() {return ibsta;}

EXPORT int FDTI_error_ibcnt() {return ibcnt;}

EXPORT char* FDTI_bios_ver() {return (char*) bios_ver;}

size_t usb_read(int fd, void *buf, size_t count, unsigned timeout_ms)
{
struct pollfd fds;
fds.fd = fd; fds.events = POLLIN;
switch (poll(&fds, 1, timeout_ms)) {
	case -1 :	// error
		break;
	case  0 :	//  timeout
		break;
	default:	//  OK to read
		if (read(fd, buf, count)) {/*error condition*/};
		break;}

}

int GPIB_open_socket(char *IP_address, int port)
{
    int sock;                        /* Socket descriptor */
    struct sockaddr_in echoServAddr; /* Echo server address */
    unsigned short echoServPort;     /* Echo server port */
    char *servIP;                    /* Server IP address (dotted quad) */
    char *echoString;                /* String to send to echo server */
//     char echoBuffer[RCVBUFSIZE];     /* Buffer for echo string */
    unsigned int echoStringLen;      /* Length of string to echo */
    int bytesRcvd, totalBytesRcvd;   /* Bytes read in single recv() 
                                        and total bytes read */

    if (IP_address == NULL) return(-1);

    servIP = IP_address;             /* First arg: server IP address (dotted quad) */
//     echoString = argv[2];         /* Second arg: string to echo */

    echoServPort = port; /* Use given port */

    /* Create a reliable, stream socket using TCP */
    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        return -1;

    /* Construct the server address structure */
    memset(&echoServAddr, 0, sizeof(echoServAddr));     /* Zero out structure */
    echoServAddr.sin_family      = AF_INET;             /* Internet address family */
    echoServAddr.sin_addr.s_addr = inet_addr(servIP);   /* Server IP address */
    echoServAddr.sin_port        = htons(echoServPort); /* Server port */

    /* Establish the connection to the echo server */
    if (connect(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
        return -1;
    return sock;
}
