#include <predef.h> // standard Netburner includes for Mod5270 follow
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <startnet.h>
#include <tcp.h>
#include <init.h>
#include <autoupdate.h> // required for netburner reprogramming over ethernet
// #include <ftpd.h> // following includes not needed for serial I/O
// #include <htmlfiles.h>
#include <taskmon.h>
#include <..\MOD5270\system\sim5270.h> // Mod5270 hardware specific include to allow direct I/O
#include <dhcpclient.h> // required for dhcp IP assignment
#include <math.h>
#include <bsp.h>  // required only for ForceReboot() operation after communication failure
#include <..\MOD5270\include\pinconstant.h> // for gpio lines such as chip selects
#include <pins.h> // for gpio lines such as chip selects
#include <vector>
#include <iostream>

// NMR netburner code for USB communication using FTDI USB <-> Serial interface
// Commands processed via scanf("%d %f %d %f %d %d %d", &dac1, &volt1, &dac2, &volt2, &adcnum, &nreq, &nfreq,  &atten, &onoff, &gain);
// providing access to 4 DACs, 4 ADCs, I2C volt and temp, RF attenuator and digital controls
// Hardware interface to CMODS6 done by mapping netburner VME data bus pins to DAC devices
// Netburner simply controls DAQ devices over serial and parallel lines through the FPGA which acts as a multiplexer and bus driver
//		FPGA also handles module addressing, drives diagnostic LEDs, can automate some activities
// Limitation is simply the number of VME lines available (16) of which 7 are available as outputs and 9 are inputs
// Address bus has 7 outputs and one input
// Data bus is set as 8 bit bidirectional bus
//
// We use an approach similar to the enhanced parallel port (EPP)
// In this mode of operation, communication is done over an 8 bit bidirectional data bus between Netburner and two FPGA registers:
// 		FPGA Address register, read/write, stores the 2 bit address of the selected data register (A,B,C,D Cmod S6 ports)
//		FPGA Data register, read/write, provides direct R/W access to the hardware port currently selected in the address register
// So, communication is done by first writing the FPGA port address to the address register, then reading or writing to the data register
// Using this method, a large number of devices attached to the FPGA can be accessed. We only need 4 ports (8 bits each) for everything
// The hardware ports themselves are setup with fixed bit directions, most bits are outputs, plus a few inputs
// Operations:
// 		Write FPGA address register, stores 2 bit port address, PORTA = 0, PORT B = 1, PORTC = 2, PORTD = 3
// 		Read address register, reads back port address. Only used for verification of write
//		Read data register, reads all pins for the selected port, includes input and outputs
//		Write data register, sets only output pins for the selected port
// These functions can be used to verify all data transfers, as well
//
// Operations are controlled by the following netburner address bus lines. All are outputs except for Busy:
// 		AEN address enable, indicates that the netburner is supplying a valid VME module address
// 		A0 address bit 0, 3 address bits allow up to 8 NMR modules to be individually controlled
// 		A1 address bit 1
// 		A2 address bit 2
// 		WR (active low, DBUS_IN) write mode, when netburner is writing to FPGA, controls data bus direction, otherwise read mode
//			Netburner and FPGA both must reverse bus direction
// 		ASTB address strobe, netburner is accessing the address register (latched on rising edge)
// 		DST data strobe, netburner is accessing the data register (latched on rising edge or simply open latch when true)
// 		Busy bit (active low) indicates to the netburner that the a DAQ board address matches the address bits and is listening or talking
//			also used to read back ADC data by "OR" of ADC data bit with active busy, ADC must not interfere with initial address response
//
// New code:
//		Switched to EPP model for bus transfers
//		Added inputs for attenuation value (6 bit integer, 0.5 dB steps) and 4 bit integer to control gains and switches
//		Created assignments for 4 8 bit ports, A-D
//		Added address register write before accessing each device
//		Added subroutine for programming SPI attenuator and 4 bits for digital controls
//		Added SPI routines for attenuator, similar to DAC code
//		Added SPI routines for I2C, similar to ADC code
//
extern "C" {
void UserMain(void * pd);
}

const char * AppName="nmrFM_tcp"; // name of netburner application file
#define TCP_LISTEN_PORT	23   // Telnet port number
//#define RX_BUFSIZE (4096)
#define RX_BUFSIZE (80)
#define TX_BUFSIZE (8192)
//----- Global Vars -----
int fdnet; // TCP network file descriptor
char RXBuffer[RX_BUFSIZE]; // TCP receive buffer
char RXI2C[RX_BUFSIZE]; // I2C buffer
char RXSDEV[RX_BUFSIZE]; // stand dev buffer
std::string RXBuffer1; // TCP receive buffer
std::vector<std::string> buffer; 
char TXBuffer[TX_BUFSIZE]; // TCP transmit buffer
// global variables and assignments
unsigned char addr_state = 0; // set initial address bus state to 0, do not confuse with the following variable!
unsigned char state = 0; // set initial data bus state to 0
// CMOD S6 bit assignments
// PORT D (3), ADC
unsigned short sclockadc = 0x02, sdataadc = 0x04, sdataadc_in = 0x08, busyadc_in = 0x10, sstartadc = 0x01; // CMOD ADC data, clock and control bits
// PORT B (1), DAC
unsigned short sclockdac = 0x02, sdatadac = 0x04, sdatadac_in = 0x10, sclrdac = 0x08, syncdac = 0x01; // CMOD DAC data, clock and control bits
// PORT C (2), I2C
unsigned short sclocki2c = 0x04, sdatai2c = 0x01, sdatai2c_in = 0x02; // CMOD I2C data and clock bits
// PORT A (0), digital I/O and attenuator
unsigned short sclockatt = 0x01, sdataatt = 0x02, slatchatt = 0x04; // CMOD attenuator DAC data, clock and control bits
unsigned short sw1 = 0x08, sw2 = 0x10, gain1 = 0x20, gain2 = 0x40; // CMOD switch and gain bits
// Netburner address and data bus assignments
unsigned short dbus_in = 0x40, aen = 0x08, a0 = 0x01, a1 = 0x02, a2 = 0x04, astb = 0x10, dstb = 0x20, busy = 0x80; // address
// choose module address
unsigned modadd = 0x02; // NMR module address set arbitrarily to 2 for now, will eventually be passed as calling argument
// i2c clock state  and data bit state registers
unsigned short dstate=0x01, cstate=0x00; // i2c initial state has SDA high and SCL low
double ADCave[1000]; // average voltage

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// common I/O subroutines
void outporta(unsigned char value)  // set netburner control lines, all outputs except for busy
{
  sim.gpio.podr_datah = value; // write netburner address bus
}
int inporta(void)  // read netburner control lines
{
  return sim.gpio.ppdsdr_datah;  // read netburner address bus, mainly to examine BUSY
}
void outportb(unsigned char value)  // write to FPGA via data bus lines, must be in bus write mode already with port address already stored
{	// writes through data register to selected port A-D
  sim.gpio.podr_datal = value; // write netburner data bus
  // addr_state = addr_state | dstb;
  sim.gpio.podr_datah = addr_state | dstb; // write data strobe
  // outporta(addr_state | dstb); // cycle data strobe up and down to store port #, slow version, fast version is to leave dstb up until writes completed
  // write to FPGA complete
  // addr_state = addr_state & ~dstb;
  sim.gpio.podr_datah = addr_state & ~dstb; // clear data strobe
  // outporta(addr_state & ~dstb);
}
int inportb(void)  // read from FPGA data bus lines, must be in bus read mode already with port address already stored
{	// reads selected port A-D through data register
  addr_state = addr_state | dstb;
  outporta(addr_state); // cycle data strobe up and down to store port #, slow version
  // read from FPGA complete
  addr_state = addr_state & ~dstb;
  outporta(addr_state);
  return sim.gpio.ppdsdr_datal;  // read netburner data bus, mainly for serial or busy readback, data is valid until next bus operation
}

// correct sequence for changing data bus directions is:
//		from netburner read to write - change bus drive direction turning FPGA bus to input, then change netburner outputs to write
//		from netburner write to read - change netburner outputs to read, then change bus direction allowing FPGA to write
void setoutput(void) { // switch data bus to output (netburner write)
	addr_state = addr_state & ~dbus_in; // set bus direction control for Netburner write, FPGA read
	outporta(addr_state);
	sim.gpio.pddr_datal = 0xFF;    // Set the databus 7-0 to be netburner data outputs
}
void setinput(void) { // switch data bus to input (netburner read)
	sim.gpio.pddr_datal = 0x00;    // Set the data bus 7-0 to be netburner data inputs
	addr_state = addr_state | dbus_in; // set bus direction control for Netburner read, FPGA write
	outporta(addr_state);
}

void setaddr(unsigned char port) { // select FPGA port # to access via address register
	setoutput(); // change to write operation
	sim.gpio.podr_datal = port; // write netburner data bus with FPGA port #
	addr_state = addr_state | astb;
	outporta(addr_state); // cycle address strobe up and down to store port #
	// write complete
	addr_state = addr_state & ~astb;
	outporta(addr_state);
}
int getaddr(void) { // retrieve FPGA port # via address register
	setinput(); // change to read operation
	addr_state = addr_state | astb;
	outporta(addr_state); // cycle address strobe up and down to get port #
	// read complete
	addr_state = addr_state & ~astb;
	outporta(addr_state);
	return inportb(); // data bus has port value, held until next bus operation
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ADC subroutines, data bus is currently write for all operations to avoid changing bus direction

int BUSYadc(void)  // read ADC busy input, low when busy
{
//	  int isbusy;
//  switch bus direction to read
    /* setinput();
    isbusy = inportb() & busyadc_in;
    setoutput(); // back to output
    return isbusy; */
	inportb();
	return 1; // ignore busy for now to avoid changing bus direction, ADC is usually already finished, can be turned on above
}
int SDAadc_IN(void)  // read ADC serial data input bit
{
/*	int adcbit;
// modified to read busy control line instead of port
// switch bus direction to read
    setinput();
	adcbit =  inportb() & sdataadc_in;
    setoutput(); // back to output
    return adcbit; */
	return inporta() & busy; // use busy line to read ADC bit, much faster
}

void spiadc_start(void)  // setup ADC SPI bus values, all low
{
  state = 0; // clear START, SDA, SCL
  outportb(state);
  return;
}
void spiadc_stop(void) // clean up ADC spi values, set bus low
{
	spiadc_start(); // stop is same as start for ADC
}
int spiadc_trig(void) // trigger ADC conversion and wait for busy to show complete
{
  state = state | sstartadc;  // start a conversion
  outportb(state);
  state = state & ~sstartadc;
  outportb(state);
// wait here for busy to complete
  while (!BUSYadc()) {
  };
  return 1; // return ADC busy flag, high if conversion completed
}

signed short int spiadc_tx(unsigned long d)  // transmit ADC 16 bit data word and receive 16 bit data output on rising clock edge
{
int x;
signed short rcv=0; // 16 bit read word
  if (d&0x8000) state = state | sdataadc; // pick msb of transmit word to ADC and write bit to databus
  else state = state & ~sdataadc;
  outportb(state);
  for(x=16; x; x--) { // keep this loop as fast as possible, as time spent here determines ADC conversion speed!
	  	  	  	  	  // all bus I/O done directly via inline hardware access (sim.gpio...)
    rcv <<= 1;  // left shift received bits
    if(sim.gpio.ppdsdr_datah & busy) rcv |= 1; // read new ADC output data bit and combine read bits
    // state = state | sclockadc;
    // set clock bit and write to databus, only update state variable with data bit, not clock bit, don't update addr_state at all
    // 		in an attempt to improve speed slightly
    sim.gpio.podr_datal = state | sclockadc; // write ADC clock to netburner data bus, inline code for best speed
    sim.gpio.podr_datah = addr_state | dstb; // write data strobe to address bus
    sim.gpio.podr_datah = addr_state & ~dstb; // clear data strobe
    d <<= 1; // shift transmit bit left
    // if (d&0x8000) state = (state | sdataadc) & ~sclockadc ; // pick next bit and combine with cleared clock bit
    // else state = (state & ~sdataadc) & ~sclockadc;
    if (d&0x8000) state = (state | sdataadc); // pick next bit of transmit word and combine without clock bit
    else state = (state & ~sdataadc);
    sim.gpio.podr_datal = state; // write netburner data bus
    sim.gpio.podr_datah = addr_state | dstb; // write data strobe
    sim.gpio.podr_datah = addr_state & ~dstb; // clear data strobe
  }
  return rcv; // return 16 bit signed ADC read data
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DAC subroutines
void SDAdac(unsigned short value)  // set DAC serial data output
{
	if (value == 1) state = state | sdatadac;
	else state = state & ~sdatadac;
	outportb(state);
	return;
}
void SCLdac(unsigned short value)  // set DAC serial clock output
{
	if (value == 1) state = state | sclockdac;
	else state = state & ~sclockdac;
	outportb(state);
	return;
}
void SYNCdac(unsigned short value)  // set DAC sync output, latches DAC data
{
	if (value == 1) state = state | syncdac;
	else state = state & ~syncdac;
	outportb(state);
	return;
}
void SCLRdac(unsigned short value)  // set DAQ sclr output, clears all DACs when low!
{
	if (value == 1) state = state | sclrdac;
	else state = state & ~sclrdac;
	outportb(state);
	return;
}
int SDAdac_IN(void)  // read DAC serial data readback bit, useful for transmission verification
{
	// int rdata;
	//  switch bus direction to read
	//	setinput();
	//  rdata = inportb() & sdatadac_in;
	//  setoutput(); back to output
	// return rdata;
	return 0; // dac read disabled for now to avoid switching bus direction
}

void spidac_start(void)  // setup DAC SPI bus values
{
	// set all outputs high
	// SYNCdac(1);
	// SCLRdac(1);
	// SDAdac(1);
	// SCLdac(1);
	state = state | 0x0f;
	outportb(state);
	// don't cycle SCLR or all DAC values will be lost!
//	SCLRdac(0); // clear DAC
//	SCLRdac(1);
	return;
}
void spidac_stop(void) // DAC clean up
{
	// all outputs high
	//SYNCdac(1);
	//SCLRdac(1);
	//SDAdac(1);
	//SCLdac(1);
	spidac_start();
	return;
}

unsigned long spidac_tx(unsigned long d)  // DAC transmit 24 bit data word and receive 24 bit data output on falling clock edge
{
	int x;
	unsigned long rcv = 0;
	SYNCdac(0);  // starts DAC
	for (x = 24; x; x--) {
		// rcv <<= 1;  // left shift received bits
		if (d & 0x800000) SDAdac(1); // pick transmit msb
		else SDAdac(0);
		SCLdac(0); // transmit bit
		// if (SDAdac_IN()) rcv |= 1; // combine read bits
		d <<= 1; // shift transmit bit
		SCLdac(1);
	}
	SDAdac(1);
	SYNCdac(1);  // stops DAC
	return rcv; // return 24 bit data
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Attenuator subroutines, all outputs
void SDAatt(unsigned short value)  // set ATTEN serial data output
{
  if (value==1) state = state | sdataatt;
  else state = state & ~sdataatt;
  outportb(state);
  return;
}
void SCLatt(unsigned short value)  // set ATTEN serial clock output
{
  if (value==1) state = state | sclockatt;
  else state = state & ~sclockatt;
  outportb(state);
  return;
}
void SLATCHatt(unsigned short value)  // set ATTEN serial latch output, latch on true
{
  if (value==1) state = state | slatchatt;
  else state = state & ~slatchatt;
  outportb(state);
  return;
}

void spiatt_start(void)  // setup ATTEN SPI bus values all low
{
  SDAatt(0);
  SCLatt(0);
  SLATCHatt(0);
  return;
}
void spiatt_stop(void) // ATTEN clean up, set bus low
{
	spiatt_start(); // stop is same as start
}

void spiatt_tx(int d)  // transmit ATTEN 6 bit data word on rising clock edge, latch when finished
{
int x;
  for(x=6; x; x--) {
    if (d&0x0020) SDAatt(1); // pick msb
    else SDAatt(0);
    SCLatt(1); // transmit bit
    d <<= 1; // shift transmit bit
    SCLatt(0);
  }
  SDAatt(0);
  SLATCHatt(1); // cycle latch bit hi
  SLATCHatt(0);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// i2c subroutines, communicate with voltage and temperature sensors
// We can use a small delay routine between SDA and SCL changes to give a clear sequence on the I2C bus, done by reading sdata_in.
void i2c_dly(void)
{
// longer delay here, if needed
  return;
}
void SDA_i2c(unsigned short value)  // set I2C serial data output
{
  setoutput();  // write
  dstate = (value*sdatai2c);
  outportb(dstate|cstate); // combine clock and data bits
  //printf("sda d %x c %x \n",dstate,cstate);
  return;
}
void SCL_i2c(unsigned short value)  // set I2C serial clock output
{
  setoutput(); // write
  cstate = (value*sclocki2c);
  outportb(dstate|cstate); // combine clock and data bits
  //printf("sdc d %x c %x \n",dstate,cstate);
  return;
}
int SDA_IN_i2c(void)  // read I2C serial data input, note that DAQ hardware separates I2C serial data into input and output lines
					// output must be set high (acts as soft pullup) to allow readback of input data
{
  setinput(); // read
//  printf("status = %x \n",inportb(port5));
  return (inportb()&sdatai2c_in);
}

// The following 5 functions provide the primitive start, stop, read and write sequences. All I2C transactions are built up from these.
void i2c_start(void)  // transmit standard i2c start or restart bit sequence, all writes
{
  SDA_i2c(1);
  i2c_dly();
  SCL_i2c(1);
  i2c_dly();
  SDA_i2c(0);
  i2c_dly();
  SCL_i2c(0);
  i2c_dly();
  return;
}
void i2c_stop(void) // transmit standard i2c stop bit sequence, all writes
{
  SDA_i2c(0);
  i2c_dly();
  SCL_i2c(1);
  i2c_dly();
  SDA_i2c(1);
  i2c_dly();
  return;
}
unsigned char i2c_rx(char ack)  // read I2C data byte and control acknowledge bit, reads and writes
{
  char x, d=0;
  SDA_i2c(1);
  for(x=0; x<8; x++) {
    d <<= 1;
    SCL_i2c(1);
    i2c_dly();
    if(SDA_IN_i2c()) d |= 1;
    SCL_i2c(0);
  }
  if(ack) SDA_i2c(0);
  else SDA_i2c(1);
  SCL_i2c(1);
  i2c_dly();             // send (N)ACK bit if requested
  SCL_i2c(0);
  SDA_i2c(1);
  return d;
}
unsigned char i2c_tx(unsigned char d)  // transmit I2C data byte and monitor acknowledge bit, reads and writes
{
  char x;
  static char b;
  for(x=8; x; x--) {
    if(d&0x80) SDA_i2c(1);
    else SDA_i2c(0);
    SCL_i2c(1);
    d <<= 1;
    SCL_i2c(0);
  }
  SDA_i2c(1);
  SCL_i2c(1);
  i2c_dly();
  b = SDA_IN_i2c();          // possible ACK bit if available
  SCL_i2c(0);
  return b;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ADC main code below for LTC1859 quad 16 bit SPI ADC
double UserADC(int adcnum, int nreq, int nloop) { // input # reads and freq count
  signed short int readword;  // dummy readback data
  unsigned long writeword;  // write data
  unsigned long config = 0x0400;  // ADC configuration data
  unsigned long ADCAddress = 0x0000; // ADC address
  signed short int ADCValue; // 16 bit signed (2's complement) ADC value
  double ADCVoltage; // ADC Voltage
  double ADCSum = 0.; // sum of ADC values
  //double ADCave; // average voltage
//  double ADCSumSQ = 0.; // sum of ADC**2
  double ADCsdev = 0.; // stand dev of voltage
  double ADCEntries; // # of ADC reads
  int nreads = 1; // loop counter for number of reads
  unsigned char port = 0x03; // ADC port address
  static int first = 1; // initialization

  setaddr(port); // select port # to access via address register and switch to output mode
  /* if (adcnum >= 0 && adcnum < 4) ADCAddress = 0x0000 + 0x1000 * adcnum;
  else iprintf("ADC number out of range, using 0 instead \n"); */
  ADCAddress = 0x1000 * adcnum;
  /* if (nreq > 0) nreads = nreq;
  else iprintf("# reads incorrect, using 1 instead \n"); */
  nreads = nreq;
  ADCEntries = (double) nreads;
  state = 0; // initialize state
  spiadc_start();              // initialize SPI port values
  writeword = config | ADCAddress; // ADC 0

  // start SPI writes and reads, not sure if following is needed other than for initialization
    if (first) { // only initialize once
  	  if (!spiadc_trig()) iprintf("ADC still busy? \n"); // trigger conversion to synchronize ADC
  	  readword = spiadc_tx(writeword); // write to control register, 1st readword is junk (old)
  	  first = 0;
    }

// loop here over number of reads
  while (nreads > 0) {
  	if (!spiadc_trig()) iprintf("ADC still busy? \n"); // trigger conversion and check busy flag
  	ADCValue = spiadc_tx(writeword); // write to control register, readback 1st ADC value
  	ADCVoltage = ((float)(ADCValue) / 65536.) * 20.; // calculate voltage (bipolar, +- 10V range)
  	ADCSum = ADCSum + ADCVoltage;
  	// ADCSumSQ = ADCSumSQ + ADCVoltage*ADCVoltage;
  	// printf("ADC %d, voltage is %-6.3f, 2's compl value = %x \n", adcnum, ADCVoltage, ADCValue);
  	nreads--;
  }
// until here, now calc mean and stand dev
  ADCave[nloop] = ADCSum / ADCEntries;
  // ADCsdev = sqrt((1./ADCEntries)* (ADCSumSQ - 2.*ADCave[nloop]*ADCSum + ADCEntries*ADCave[nloop]*ADCave[nloop]));
  //printf("term1 %-6.3f \n",(1./ADCEntries));
  //printf("term3 %-6.3f \n",(2.*ADCave*ADCSum));
  //printf("term4 %-6.3f \n",(ADCEntries*ADCave*ADCave));
  //printf("ADCSumSQ %-6.3f, ADCSum %-6.3f \n", ADCSumSQ,ADCSum);
  //printf("%d %-7.4f %-7.4f \n", nloop, ADCave, ADCsdev);
  //printf("%-7.4f %-7.4f \n", ADCave, ADCsdev);
  //printf("%-6.3f\n", ADCave[nloop]);
  //spiadc_stop();               // finish up by setting port values low
  return ADCsdev;
// done
} // end ADC
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// dac main code below, sets any two DAC outputs currently, AD5754 quad 16 bit SPI DAC
void UserDAC(int dac1, float volt1, int dac2, float volt2) {
	unsigned long readword;  //readback data
	unsigned long config = 0x190005;  // configuration data
	unsigned long DACAddress; // DAC address
	unsigned long DACdata; // data contains the DAC address and requested voltage
	//unsigned long NOP = 0x180000;  // NOP function for readback
	unsigned long output = 0x0C0004; // Output range data
	unsigned long power = 0x10000F; // power up DACs
	// unsigned long LoadDACs; // load DACs
	// unsigned long ClearDACs; // clear DACs
	// unsigned long ReadDac = 0x800000; // read DAC register A
	// unsigned long ReadDacnum; // read DAC register 
	signed int DACValue; // 16 bit signed (2's complement) DAC value
	float DACVoltage; // DAC set to voltage
	int dacnum; // user specified DAC
	float voltage; // user specified output voltage
	unsigned char port = 0x01; // DAC port address
	static int first = 1; // initialization

	setaddr(port); // select port # to access via address register and switch to output mode
	//config = 0x190000 | 0x1 | 0x4; //config = CTRL address | SDO Disable | Clamp Enable
	//output = 0x0C0004; // +-10V range
	//power = 0x10000F; // initialize all DACs to ON
	// LoadDACs = 0x1D0000;
	// ClearDACs = 0x1C0000;
	state = sclrdac; // initialize state, don't want to ever clear dac

	// first DAC setting
	dacnum = dac1;
	voltage = volt1;
	/*if (abs(voltage) <= 10.) DACVoltage = voltage; //
	else {
		// iprintf("Voltage out of +/- 10V range, using 0 instead \n");
		DACVoltage = 0.;
	} */
	DACVoltage = voltage;
	DACValue = int((65536.*(DACVoltage + 10.)) / 20.) ^ 0x8000; // convert voltage to digital, 2's complement, XOR of msb
	/*if (dacnum < 4 && dacnum >= 0) DACAddress = 0x010000 * dacnum; // set DAC address
	else {
		// iprintf("DAC number of range, using 0 instead! \n");
		DACAddress = 0x000000;
	} */
	DACAddress = 0x010000 * dacnum;
	DACdata = DACAddress | DACValue; // combine address, DAC value
	// ReadDacnum = ReadDac + DACAddress; // combine address, DAC value and DAC #
	spidac_start();              // initialize SPI port values
	// first do SPI writes
	if (first) { // only initialize once
		readword = spidac_tx(config); // write to control register
		readword = spidac_tx(output); // write to voltage range register, +-10V for all 4
		readword = spidac_tx(power); // write to power on/off register, all 4 DACs on
		first = 0;
	}
	readword = spidac_tx(DACdata); // write to DAC register
	// readback only used for diagnostics
	// then a read, requires two writes, first to select register, then noop to read it
	//readword = spidac_tx(ReadDacnum); // setup read for DAC register
	//readword = spidac_tx(NOP); // perform SPI readback
	//readword = readword & 0xFFFF; // keep only 16 bit value for DAC register read
	//printf("DAC %d, voltage %-6.3f, 2's compl value = %x, readback = %x \n", dacnum, DACVoltage,
	//	DACValue, (int)readword);
	
	// second DAC setting, done same as first, don't have to repeat setup steps
	// only perform if DAC value is updated
	if (!(dac1==dac2 && volt1==volt2)) {
		dacnum = dac2;
		voltage = volt2;
		/* if (abs(voltage) <= 10.) DACVoltage = voltage; //
		else {
			// iprintf("Voltage out of range, using 0 instead! \n");
			DACVoltage = 0.;
		} */
		DACVoltage = voltage;
		DACValue = int((65536.*(DACVoltage + 10.)) / 20.) ^ 0x8000; //2's complement, XOR of msb
		/* if (dacnum < 4 && dacnum >= 0) DACAddress = 0x010000 * dacnum;
		else {
			// iprintf("DAC number of range, using 0 instead! \n");
			DACAddress = 0x000000;
		} */
		DACAddress = 0x010000 * dacnum;
		DACdata = DACAddress | DACValue;
		// ReadDacnum = ReadDac + DACAddress;
		readword = spidac_tx(DACdata); // write to DAC register
	}
	// diagnostics
	// readword = spidac_tx(LoadDACs); // Load all DACs, voltages should appear at DAC outputs, not necessary with LDAC grounded
	// then a read, requires two writes, first to select register, then noop to read it
	//readword = spidac_tx(ReadDacnum); // setup read for DAC register
	//readword = spidac_tx(NOP); // perform SPI readback
	//readword = readword & 0xFFFF; // keep only 16 bit value for DAC register read
	//printf("DAC %d, voltage %-6.3f, 2's compl value = %x, readback = %x \n", dacnum, DACVoltage,
	//	DACValue, (int)readword);
	// spidac_stop();               // finish up by setting port values high
	//
	// done
} // end DAC
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// attenuator (Minicircuits DAT-31-SP, SPI) and switches (digital) main code
void UserATTSW(int onoff,int gain, int atten) { // set RF switches and gains, RF switch = 1/0 for on/off, LF gain = 1/0 for high/low gain
	// set attenuator value, 6 bits * 0.5 dB = 0-31.5 dB, SPI clocked on positive edge
	unsigned char port = 0x00; // ATT + SW port address
// just set 4 bits in port for switches and gains
	setaddr(port); // select port # to access via address register and switch to output mode
	state = !onoff*sw1 + onoff*sw2 + gain*gain1 + gain*gain2; // initialize state and set switches, sw1 is 50 ohm term, sw2 is tuner and coil
	outportb(state);
// now handle attenuator, clock on positive edge as for ADC
	spiatt_start();             // initialize SPI port values
	spiatt_tx(atten); 			// write to control register
	spiatt_stop();              // finish up by setting att port values low
} // end attenuator
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// I2C main code, DS2782 IVT monitor
void UserI2C() { // read I2 DACs for voltage and temperature for now
	unsigned char port = 0x02; // I2C port address
	int thibyte, tlobyte, vhibyte, vlobyte;
	double temp, volt;
	//
	setaddr(port); // select port # to access via address register and switch temporarily to output mode
	//
	// The 5 primitive i2c functions above can easily be put together to form complete I2C transactions.
	// The following example shows how to read the T, V and I from DS2782 registers 0A and 0B.
	// Transfers are byte wide operations, readback data is two bytes wide
	//
	//printf("DS2782 I2C Test Routine \n");
	state = 0; // initialize state
	i2c_start(); // send start sequence
	if (i2c_tx(0x68)==1) printf("no address acknowledge \n"); // send DS2782 I2C address with R/W bit cleared
	if (i2c_tx(0x0A)==1) printf("no register acknowledge \n"); // send temp sensor register address, high byte
	i2c_start(); // send a restart sequence
	if (i2c_tx(0x68+1)==1) printf("no read acknowledge \n"); // send I2C address with R/W bit set
	thibyte = i2c_rx(1);  // get temp high byte and send acknowledge. Internal register address will increment automatically.
	tlobyte = i2c_rx(1);  // get low byte of the range
	vhibyte = i2c_rx(1);  // get volt high byte and send acknowledge. Internal register address will increment automatically.
	vlobyte = i2c_rx(0);  // get low byte of the range, no acknowledge
	i2c_stop(); // send stop sequence

	//  printf("temp byte high %x, low %x \n",thibyte,tlobyte);
	temp = 0.125 * ((thibyte<<3) + (tlobyte>>5)); // positive temp only, 1/8 degree resolution
	// printf("temp is %-5.1f C \n",temp);
	//  printf("volt byte high %x, volt byte low %x \n",vhibyte,vlobyte);
	volt = 0.00488 * ((vhibyte<<3) + (vlobyte>>5)); // positive volts only, 5 mV resolution
	// printf("volt is %-6.3f V \n",volt);
	// printf("%-6.2f %-6.3f \n",temp,volt);
	sprintf(RXI2C, "%-6.2f %-6.3f\r\n",temp,volt);
	// printf("%s",RXBuffer);
	 writestring(fdnet, RXI2C); //cak
	// fflush(stdout); // flush output buffer

} // end I2C
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main routine
void NMRMain() {
	int adcnum, nreq, nfreq; // arguments for ADC and DAC routines
	int atten, onoff, gain; // attenuation value (2*dB), RF switch (0 off or 1 on), gain switch (0 low or 1 hi)
	float volt1, volt2, volt3, volt4, vstep; // NMR tune, IF offset, phase and log offset, voltage step for FM mode
	int dac1 = 0; // DAC #1 used for IF offset
	int dac2 = 1;// DAC #2 used for tune
	int dac3 = 2; // DAC #3 used for phase adjust
	int dac4 = 3; // DAC #4 used for log offset
	int nloop = 0; // loop counter for frequency value
	int nsweep = 9; // number of sweeps - 1
	int nloop2 = 0; // sweep loop counter
	float ADC1[1000]; // ADC values averaged over multiple sweeps
	double ADCsdev; // stand dev of voltage
	double SUMsdev; // sum of stand dev
	static int first = 1; // initialization
	// RXBuffer carries command and returns output

	// int ndelay = 100; // delay for step pulse width
	// int nloop2; // loop counter for step pulse
	// float volt3_0 = 0.; // TTL false
	// float volt3_5 = 5.; // TTL true, steps frequency to next value
	// float volt3 = 7.; // phase
	// float volt3 = 6.5; // phase
	// float volt4 = -5.84; // log offset

	// simple main routine to call ADC and DAC routines, sets DACs and reads one ADC

	//setbuf(stdout,NULL); // turn off serial buffering

	if (first) { // only initialize once
	// sim.gpio commands allow direct access to netburner I/O pins in two 8 bit banks
		sim.gpio.par_ad &= ~(0x1);     // Set the netburner 15-0 pins to GPIO
		sim.gpio.pddr_datal = 0x00;    // Set the data bus 7-0 to be data inputs
		sim.gpio.pddr_datah = 0x7F;    // Set the address bus 15-8 to be address outputs + busy input
	// VME bus has sixteen lines used, 8 on data bus and 8 on address bus
	// we use the address bus as control lines and data bus as bidirectional 8 bit bus

	// setup chip selects
		J1[5].function( PINJ1_5_GPIO ); // configure CS1 for spill input
		J1[5].hiz(); // configure as input
		J1[7].function( PINJ1_7_GPIO ); // configure CS3 for spare input
		J1[7].hiz(); // configure as input
		J1[6].function( PINJ1_6_GPIO ); // configure CS2 for freq step output
		J1[6].drive(); // configure as output
		J1[6] = 0; // set low

	// setup control (address) and data buses
		addr_state = 0; // set bus direction control line (0x40) = 0 for Netburner write, FPGA read
		outporta(addr_state);   // initialize 8 bit address bus low, data bus xcvr as netburner output (low)
	// switch netburner data bus direction to write
		sim.gpio.pddr_datal = 0xFF;    // Set the databus 7-0 to be data outputs
	// netburner can now write to databus with FPGA as listener
		sim.gpio.podr_datal = 0xFF; // set netburner data bus high to minimize load on bus driver, leave DSTB inactive
	// set requested module address bits, A0,1,2
		addr_state = addr_state | (modadd & a0) | (modadd & a1) | (modadd & a2); // fixed address for now, will eventually be passed to netburner
		outporta(addr_state);
	// set address enable  AEN
		addr_state = addr_state | aen; // set address enable
		outporta(addr_state);
		outporta(addr_state); // provide a little more time for response
	// check for module response via BUSY
		if ((inporta() & busy) == busy) { // valid for busy = 0, active low
			printf("Module %d did not respond!\n",modadd); // communication will fail
		// could loop here until a valid response (change if to while), but for now continue
		}
		first = 0;
	}
	// else printf("Module %d responded OK\n",modadd); // ready to start communicating
	// now ready to access hardware
	// should still be in NB write state, but no data bus access has occurred
	// state should be static and have only AEN, modadd lines on, check FP LEDs. Busy should be high w'o DAQ card
	// Check front panel leds here, data bus high, address bus as set above, will be stable until command input
	/* dac1 = 0; // kludges for testing
	volt1 = 1.;
	dac2 = 1;
	volt2 = -1.;
	atten = 1;
	onoff = 1;
	gain = 1;
	adcnum = 0;
	nreq = 200; // 100 samples */

	// while (1) { // loop forever setting DACs, Atten, Switches and reading ADCs, I2C
		//iprintf("?\n");
		// now reads command from RXBuffer instead of serial input
		sscanf(RXBuffer,"%f %f %f %f %d %d %d %d %d %d", &volt3, &volt2, &volt4, &volt1, &adcnum, &nreq, &nfreq, &atten, &onoff, &gain);
		// For FM mode, use DAC4 (logoff) to sweep the RF gen, voltage range -1 to +1V, Fcen = 0V
		volt4 = -1.; // set start sweep voltage
		vstep = 2./(float(nfreq)-1);
		SUMsdev = 0.;
		// printf("Vstep = %f \n",vstep);
		// scanf("%d", &nfreq); // kludge for testing
		// select two DACs (0-1) and set their voltages
		// choose ADC channel (0-3), # of samples, number of frequency steps
		// set attenuation (units 0.5 dB), RF on/off (1/0), LF gain (1/0)
		// could input NMR module address above, fixed for now

		// 1st check if input string is valid up to number of frequency steps, then talk to hardware
		if (nfreq >=1) { // got at least 7 arguments, so input probably OK
		  // each of following USER routines is responsible for handling address register, data register and data direction (both FPGA and Netburner!)
		  // can comment out routines below to test individual hardware devices
		  UserATTSW(onoff,gain,atten); // set RF switches and LF gains, set RF attenuator value
		  UserI2C(); // read DAC voltage and temperature, just print results for now
		  UserDAC(dac1,volt1,dac2,volt2); // set two DAC channels values, e.g. offset and tune controls
		  UserDAC(dac3,volt3,dac4,volt4);
		  // nfreq = 100; // 100 freq values
		  // zero out sdev sume here
		  nloop = 0; // reset loop counter
		  J1[6] = 1; // signal start
 		  while (nloop < nfreq) { // acquire an NMR spectrum
 			// eventually add read of spill input here using a NB CS and add to ADC output line to flag beam is on
 	 		ADCsdev = UserADC(adcnum,nreq,nloop); // read ADC values for NMR output, returning computed mean and stand dev from nreq samples
		    if (nloop == 0) ADCsdev = UserADC(adcnum,nreq,nloop); // fix first data point
		    ADC1[nloop] = ADCave[nloop];
		    // sum up stand dev here
		    // SUMsdev = SUMsdev + ADCsdev;
		    //printf("%-6.3f\n", ADCave[nloop]);
		    // add direct TTL output from netburner using CS to drive this step trigger, speeding up loop
		    //J1[6] = 1;
			volt4 = volt4 + vstep;
			UserDAC(dac4,volt4,dac4,volt4); // step to next freq by incrementing DAC4, leave other DACs alone
		    nloop++;
 		  }

 		  J1[6] = 0; // signal end
 		  nloop2 = 0; // reset sweep counter
 		  while (nloop2 < nsweep) {
 			  nloop = 0; // reset loop counter
 			  volt4 = -1.; //
 			  UserDAC(dac4,volt4,dac4,volt4);
 			  while (nloop < nfreq) { // acquire an NMR spectrum
 				  ADCsdev = UserADC(adcnum,nreq,nloop); // read ADC values for NMR output, returning computed mean and stand dev from nreq samples
 				  if (nloop == 0) ADCsdev = UserADC(adcnum,nreq,nloop); // fix first data point
 				  ADC1[nloop] = ADC1[nloop]+ADCave[nloop];
 				  volt4 = volt4 + vstep;
 				  UserDAC(dac4,volt4,dac4,volt4); // step to next freq by incrementing DAC4, leave other DACs alone
 				  nloop++;
 			  }
 			  nloop2++;
 		  }

 		  nloop = 0;
 		  // normalize and print sdev here
  		 SUMsdev = SUMsdev / float(nfreq);
  		 // printf("%6.4f\n", SUMsdev);
  		 sprintf(RXSDEV,"%6.4f\r\n", SUMsdev);
  		 writestring(fdnet, RXSDEV); // cak
  		 buffer.clear();
 		 while (nloop < nfreq) { // output loop
// 			printf("%-6.3f\n", ADC1[nloop]);
			// printf("%-6.3f\n", ADC1[nloop]/float(nsweep+1));
			sprintf(RXBuffer,"%-6.3f\r\n", ADC1[nloop]/float(nsweep+1));
			// writestring(fdnet, RXBuffer);
 			buffer.push_back(RXBuffer); 			 
		    nloop++;
 		 }
 		  RXBuffer1.clear();
 		  *TXBuffer = '\0';
 		  int loopcounter =0 ; // to keep track of buffer location
 		  // Write buffer for transfer
 		/* cak  for (int myloop =0; myloop < RX_BUFSIZE; myloop++){
 			  TXBuffer[myloop]=RXI2C[myloop];
 			  if(RXI2C[myloop]=='\0'){
 				  loopcounter = myloop;
 				  break; 				  
 			  }
 		  }
 		  for (int  myloop =0; myloop < RX_BUFSIZE; myloop++){
 			  TXBuffer[loopcounter+myloop]=RXSDEV[myloop];
 			  if(RXSDEV[myloop]=='\0'){
 				  loopcounter = loopcounter+myloop;
 				  break; 				  
 			  }
 		  } cak */
		
 		  for(std::vector<std::string>::iterator pos=buffer.begin(); pos!=buffer.end(); pos++) {
 			 //std::cout<<"buffer content :   "<<*pos<<"   \n";
 			 std::string temp= *pos;
 			 RXBuffer1 = RXBuffer1+temp;
 		 }
 		  int loop =0;
 		  for(std::string::iterator pos1=RXBuffer1.begin(); pos1!=RXBuffer1.end(); pos1++) {
 			  //std::cout<<"character "<<*pos1<<"\n";
 			  //cak TXBuffer[loopcounter+loop] = *pos1;
 			  TXBuffer[loop] = *pos1;
 			  loop++;
 		  }
		writeall(fdnet,TXBuffer,loop);
 		//cak writestring(fdnet, TXBuffer);
		} // end valid input
		
		else { // reboot netburner after communication error, could do something smarter to clear bad command and continue?
			printf("Bad input string: %f %f %f %f %d %d %d %d %d %d\n",volt3, volt2, volt4, volt1, adcnum, nreq, nfreq, atten, onoff, gain);
			iprintf("Stopping, Netburner has to reboot! Takes a few seconds");
			// wait 1 sec for the response and event log to get written out, then reboot.
			OSTimeDly(TICKS_PER_SECOND);
			ForceReboot();
			return;
		}

	//} // end while forever
} // end main routine

/*-------------------------------------------------------------------
Convert IP address to a string
-------------------------------------------------------------------*/
void IPtoString(IPADDR  ia, char *s)
{
	PBYTE ipb = (PBYTE)&ia;
	siprintf(s, "%d.%d.%d.%d", (int)ipb[0], (int)ipb[1], (int)ipb[2], (int)ipb[3]);
}

// Allocate task stack for UDP listen task
DWORD   TcpServerTaskStack[USER_TASK_STK_SIZE];

/*-------------------------------------------------------------------
TCP Server Task
-------------------------------------------------------------------*/
void TcpServerTask(void * pd)
{
	int ListenPort = (int)pd;
	char ch;
	char *buf;
	int secsdelay = 10; // timeout in telnet read (sec)

	// Set up the listening TCP socket
	int fdListen = listen(INADDR_ANY, ListenPort, 5);

	if (fdListen > 0)
	{
		IPADDR	client_addr;
		WORD	port;

		iprintf("?\r\n");
		while (1) // loop forever
		{
			// The accept() function will block until a TCP client requests
			// a connection. Once a client connection is accepting, the
			// file descriptor fdnet is used to read/write to it.
			// iprintf("Waiting for connection on port %d...\n", ListenPort);
			fdnet = accept(fdListen, &client_addr, &port, 0);

			// iprintf("Connected to: ");
			// ShowIP(client_addr);
			// iprintf(":%d\n", port);

			// writestring(fdnet, "Welcome to the NetBurner TCP Server\r\n");
			// writestring(fdnet, "?\r\n"); // change to just ? here
			// fflush(stdout); // flush output buffer
			// char s[20];
			// IPtoString(EthernetIP, s);
			//siprintf(RXBuffer, "You are connected to IP Address %s, port %d\r\n",
			//	s, TCP_LISTEN_PORT);
			// writestring(fdnet, RXBuffer);

			while (fdnet > 0)
			{
				/* Loop while connection is valid. The read() function will return
				0 or a negative number if the client closes the connection, so we
				test the return value in the loop. Note: you can also use
				ReadWithTimout() in place of read to enable the connection to
				terminate after a period of inactivity. Existing read command
				blocks until data avail or connection terminates.
				NMR main no longer has infinite loop, but is called from here after
				each buffer read
				*/
				int n = 0;
				do {
					buf = RXBuffer; // point to beginning of buffer
				    for (;;) { // loop over input char to form line, can hang on unterminated input if socket is open
				        // n = read(fdnet, &ch, 1); // read one character at a time
				        // change to read with timeout here, nominally 10 sec
				        n = ReadWithTimeout(fdnet, &ch, 1, secsdelay*TICKS_PER_SECOND);
				        if (n <= 0) break; // exit for timeout or other problem
				        // else if (n == 1) *buf++ = ch; // append one char to buffer, increment location
				        *buf++ = ch; // one char only, append char to buffer, increment location
				        // if (n == 1 && ch == '\n') break; // break for completed line
				        if (ch == '\n') break; // break for completed line
				    }
				    *buf = '\0'; // terminate string
					// iprintf("Read: %s\n", RXBuffer);
					// process command buffer and write results
					if (strlen(RXBuffer) > 10) NMRMain(); // execute NMR code only if some input received, could do more
					writestring(fdnet, "?\r\n"); // send back completion and ready for next command
					// iprintf("?\r\n");
				} while (n > 0); // exit for timeout or problem

				// Close connection if socket error or timeout
				// iprintf("Closing client connection: ");
				// ShowIP(client_addr);
				// iprintf(":%d\n", port);
				close(fdnet);
				fdnet = 0;
			} // while fdnet
		} // while(1)
	} // while listen
}


/*-------------------------------------------------------------------
User Main
------------------------------------------------------------------*/
extern "C" void UserMain(void * pd)
{
	init();
	// don't know if following 5 lines are needed
	InitializeStack(); // set up tcp/ip stack
	if (EthernetIP == 0) GetDHCPAddress(); // can obtain DHCP address, prefer fixed address 192.168.1.x
	OSChangePrio(MAIN_PRIO);
	EnableAutoUpdate(); // required to reprogram netburner over ethernet versus serial
	EnableTaskMonitor();
	// Create TCP Server task
	OSTaskCreate(TcpServerTask,
		(void  *)TCP_LISTEN_PORT,
		&TcpServerTaskStack[USER_TASK_STK_SIZE],
		TcpServerTaskStack,
		MAIN_PRIO - 1);	// higher priority than UserMain

	while (1)
	{
		OSTimeDly(TICKS_PER_SECOND * 5);
	}
}

