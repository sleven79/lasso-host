// -----------------
// Lasso data server
// -----------------
// Host implementation v0.1 - device specifics for mbed implementation (without DMA support)
//
// Requires UART module (using mbed's RawSerial)

#include "lasso_host.h"

#ifdef INCLUDE_LASSO_HOST

RawSerial lasso_serial(USBTX, USBRX, 115200);
bool lasso_done = true;

static void lasso_comDone(int events) {
     lasso_done = true;
}

int32_t lasso_comSetup_mbed(void) {
    // nothing to do, UART already configured by instantiation of "Serial"
    return 0;
} 
     
int32_t lasso_comCallback_mbed(uint8_t* src, uint32_t cnt) {
    if (lasso_done) {
        lasso_done = false;
        lasso_serial.write(src, cnt, event_callback_t(lasso_comDone), SERIAL_EVENT_TX_COMPLETE);
        return 0;
    }
    return 16; // EBUSY from errno.h
}

// computes CRC-16-CCITT over buffer (polynome coefficients 0x1021/0x11021)
// the running crc in local variable c must be truncated to desired byte-width in each round
uint32_t lasso_crcCallback_mbed(uint8_t* src, uint32_t cnt) {
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
