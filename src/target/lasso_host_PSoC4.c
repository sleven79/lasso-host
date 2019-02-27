// -----------------
// Lasso data server
// -----------------
// Host implementation v0.1 - device specifics for Cypress PSoC4
// Note:
// 1) in order to use full floating point support (for printf and scanf), make sure that either
//    - newlib-nano is not the default library to link with, or
//    - newlib-nano float formatting is enabled and additional command line flag "-u _scanf_float" is set, or
//    - two command line flags "-u _printf_float" and "-u _scanf_float" are set
//   (settings to be found in Project->Build settings->ARM GCC x.y-...->Linker, additional Flash usage about 25kB, some extra RAM too)
// 2) Adjust heap size in Design Wide Ressources -> System
//    - info on required/estimated heap size can be found in lasso_host.c
// 3) Strobe size limitation = 65536 Bytes (=max DMA burst length for PSoC4)! 
 
#include "lasso_host.h"

#ifdef INCLUDE_LASSO_HOST

#include <project.h>
    

//------------------//
// Module functions //
//------------------//

// configures DMA channel for transmission on UART
int32_t lasso_comSetup_PSoC4(void)
{
    CyDmaEnable();                                              // must be called to enable DMA engine
    LASSO_DMA_Init();                                           // must be called to init specific channel (without enabling it yet)
    LASSO_DMA_SetDstAddress(0, (void *)LASSO_UART_TX_FIFO_WR_PTR);    // only destination, not source address specified at this point  
    
    return 0;
}
    

// configures and triggers DMA transmission on UART, or, if still transmitting, returns busy error code
int32_t lasso_comCallback_PSoC4(uint8_t* src, uint32_t cnt)    
{
    if (LASSO_UART_SpiUartGetTxBufferSize())
    {
        return EBUSY;   
    }
    else
    {
        LASSO_DMA_SetSrcAddress(0, src);
        LASSO_DMA_SetNumDataElements(0, cnt);
        LASSO_DMA_ValidateDescriptor(0);
        LASSO_DMA_ChEnable();         // this will fire the DMA immediately, since UART TX buffer is empty
    }
    return 0;
}


// computes CRC-16-CCITT over buffer (polynome coefficients 0x1021/0x11021)
// the running crc in local variable c must be truncated to desired byte-width in each round
uint32_t lasso_crcCallback_PSoC4(uint8_t* src, uint32_t cnt) {
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