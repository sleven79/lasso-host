// -----------------
// Lasso data server
// -----------------
// Host implementation v0.1 - device specifics for Seeed XIAO nrf52840 (implementation without DMA)
// Notes:
// - uses main Serial port of XIAO nrf52840 = Serial-over-USB-C (=not available in Main.cpp)
// - if TX buffer is full when writing to it, it may block, no error checks implemented
// - Serial.availableForWrite() seems to be always zero, so is of no use for checking before sending
// - a dedicated Thread is used for transmission
// - lassoRX() convenience function added for reception, needs to be prototyped in lasso_host_config.h
 

#include "lasso_host.h"


#ifdef INCLUDE_LASSO_HOST

#include <ArduinoBLE.h>		// for Serial
//#include <mbed.h>			
#include <rtos.h>			// for Thread
//using namespace mbed;	
using namespace rtos;	


/******************************************************************************/
/* Private defines                                                            */
/******************************************************************************/
    
// none
    
    
/******************************************************************************/
/* Private variables                                                          */
/******************************************************************************/

Thread thread_lassoTX, thread_lassoRX;
static volatile uint32_t then;

    
/******************************************************************************/
/* Private function declarations                                              */
/******************************************************************************/
    
void lassoTX(void) { 
	while (1) {
		/*
		static volatile unsigned char adv_ind = 1;
		adv_ind = 1 - adv_ind;
		digitalWrite(14, adv_ind);
		*/
		
		lasso_hostSignalFinishedCOM();	// assume transmission of last frame on Serial has finished, usually done on UART TX ready
		lasso_hostHandleCOM();

		// approximate wait to complete period (if lasso_hostHandlCOM takes more than period, skip wait)
		uint32_t now = millis();
		uint32_t period = now - then;
		if (period < LASSO_HOST_TICK_PERIOD_MS) {
			ThisThread::sleep_for(LASSO_HOST_TICK_PERIOD_MS - period);
			then += LASSO_HOST_TICK_PERIOD_MS;	
		} else {
			then = now;
		}
	}
}


void lassoRX(void) {
	while (1) {
		if (Serial.available()) {
			lasso_hostReceiveByte(Serial.read());
		}
	}
}
	  

/******************************************************************************/
/* Public function declarations                                               */
/******************************************************************************/	

// set up lasso scheduler and communication, UART and ISR clock, times out if Serial port not available (non-blocking)
int32_t lasso_comSetup_XIAO_nrf52840(void) {
	Serial.begin(LASSO_HOST_BAUDRATE);		// Serial-over-USB-C of XIAO nrf52840 (=not available in Main.cpp)
	while (!Serial) {
		if (millis()>5000) return 1;
	}

	Serial.println("Starting lasso host");
	then = millis();
	thread_lassoTX.start(lassoTX);
	thread_lassoRX.start(lassoRX);
	return 0;
}
 
 
// set up lasso scheduler and communication, UART and ISR clock, wait for Serial port to be available (blocking)
int32_t lasso_comSetup_XIAO_nrf52840_wait(void) {
	Serial.begin(LASSO_HOST_BAUDRATE);		// Serial-over-USB-C of XIAO nrf52840 (=not available in Main.cpp)
	while (!Serial);

	Serial.println("Starting lasso host");
	then = millis();
	thread_lassoTX.start(lassoTX);
	thread_lassoRX.start(lassoRX);	
	return 0;
}	
	

// configures and triggers transmission on UART, or, if still transmitting, returns busy error code
int32_t lasso_comCallback_XIAO_nrf52840(uint8_t* src, uint32_t cnt) {
	/*
    if (Serial.availableForWrite()==0) {	// seems to always return 0, so cannot use here
        return EBUSY;   
    }
	*/
		
	Serial.write(src, cnt);	// no error checks here, most simple of implementations, assuming enough space in TX buffer
    return 0;
}


// computes CRC-16-CCITT over buffer (polynome coefficients 0x1021/0x11021)
// the running crc in local variable c must be truncated to desired byte-width in each round
uint32_t lasso_crcCallback_XIAO_nrf52840(uint8_t* src, uint32_t cnt) {
    uint32_t d, s, t;
    
#if (LASSO_HOST_CRC_BYTEWIDTH == 1)
    uint8_t c = 0;
#elif (LASSO_HOST_CRC_BYTEWIDTH == 2)
    uint16_t c = 0;
#else    
    uint32_t c = 0;
#endif
    
    while (cnt--) {
        d = (uint32_t)(*src++);
        s = d ^ (c >> 8);
        t = s ^ (s >> 4);
        c = (c << 8) ^ t ^ (t << 5) ^ (t << 12);
    }
    return c;
}

#endif