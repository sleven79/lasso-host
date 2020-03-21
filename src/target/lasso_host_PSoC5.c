// -----------------
// Lasso data server
// -----------------
// Host implementation v0.1 - device specifics for Cypress PSoC5
// Note: 
// 1) in order to use full floating point support (for printf and scanf), make sure that either
//    - newlib-nano is not the default library to link with, or
//    - newlib-nano float formatting is enabled and additional command line flag "-u _scanf_float" is set, or
//    - two command line flags "-u _printf_float" and "-u _scanf_float" are set
//    (settings to be found in Project->Build settings->ARM GCC x.y-...->Linker, additional Flash usage about 25kB, some extra RAM too)
// 2) Adjust heap size in Design Wide Ressources -> System
//    - info on required/estimated heap size can be found in lasso_host.c
// 3) Strobe size limitation = 4095 Bytes (=max DMA burst length for PSoC5)! 

#include "lasso_host.h"

#ifdef INCLUDE_LASSO_HOST

#include <project.h>
    

//-------------------//
// Private Variables //
//-------------------//
static uint8 DMA_CH;
static uint8 DMA_TD;
    
    
//-------------------//
// Private functions //
//-------------------//    
    
CY_ISR(LASSO_UART_ISR) { 
    lasso_hostSignalFinishedCOM();
}    
    

//------------------//
// Module functions //
//------------------//

// configures DMA channel for transmission on UART
int32_t lasso_comSetup_PSoC5(void)
{
    #define DMA_BYTES_PER_BURST 1
    #define DMA_REQUEST_PER_BURST 1
    #define DMA_SRC_BASE (CYDEV_SRAM_BASE)
    #define DMA_DST_BASE (CYDEV_PERIPH_BASE) 
    
    // allocate single transaction descriptor
    DMA_TD = CyDmaTdAllocate();
    
    // allocate single DMA channel for transfer from SRAM to peripheral (=UART TX)
    // Note: DMA component must be called "LASSO_DMA"
    DMA_CH = LASSO_DMA_DmaInitialize(DMA_BYTES_PER_BURST,
                                     DMA_REQUEST_PER_BURST, 
                                     HI16(DMA_SRC_BASE),
                                     HI16(DMA_DST_BASE));

    if ((DMA_CH == CY_DMA_INVALID_TD) || (DMA_CH == CY_DMA_INVALID_CHANNEL)) {
        return ECANCELED;
    }
       
    // set initial transfer descriptor for channel
    CyDmaChSetInitialTd(DMA_CH, DMA_TD);
    
    // init transfer end interupt (todo)
    lasso_uart_isr_StartEx(LASSO_UART_ISR);    
    lasso_uart_isr_ClearPending();

    return 0;
}
    

// configures and triggers DMA transmission on UART, or, if still transmitting, returns busy error code
// note that cnt <= 4095 !
int32_t lasso_comCallback_PSoC5(uint8_t* src, uint32_t cnt)    
{
    //static uint8_t c = 0;
    
    if (LASSO_UART_GetTxBufferSize())
    {
        return EBUSY;   
    }
    else
    {       
        // test for broken com (careful, c increments also during advertising)
        /*
        if (c > 25) {
            c = 0;
            cnt /= 2;
        }
        else {
            c++;
        }
        */
        
        // configure transaction descriptor (transfer count = cnt, disable channel after transaction, generate transfer end signal
        CyDmaTdSetConfiguration(DMA_TD, cnt, CY_DMA_DISABLE_TD, LASSO_DMA__TD_TERMOUT_EN | CY_DMA_TD_INC_SRC_ADR);
        
        // configure transaction descriptor (transfer count = cnt), disable channel after transaction, do not generate transfer end signal
        //CyDmaTdSetConfiguration(DMA_TD, cnt, CY_DMA_DISABLE_TD, CY_DMA_TD_INC_SRC_ADR);         
        
        // set source and destination address in transaction descriptor
        // Note: UART component must be called "LASSO_UART"
        CyDmaTdSetAddress(DMA_TD, (uint16_t)((uint32)src), LO16((uint32)LASSO_UART_TXDATA_PTR));
        
        // clear pending requests, enable channel, do not preserve TD after transaction
        CyDmaClearPendingDrq(DMA_CH);        
        CyDmaChEnable(DMA_CH, 0);   // this will fire the DMA immediately, since UART TX buffer is empty
    }
    return 0;
}


// computes CRC-16-CCITT over buffer (polynome coefficients 0x1021/0x11021)
// the running crc in local variable c must be truncated to desired byte-width in each round
uint32_t lasso_crcCallback_PSoC5(uint8_t* src, uint32_t cnt) {
    uint32_t d, s, t;
    
#if (LASSO_HOST_CRC_BYTEWIDTH == 1)
    uint8_t c = 0;
#elif (LASSO_HOST_CRC_BYTEWIDTH == 2)
    uint16_t c = 0;
#else    
    uint32_t c = 0;
#endif
    
    while (cnt--) {
        d = (uint32_t)(*src++);                     // next data Byte (8 bits)
        s = d ^ (c >> 8);                           // xor data Byte into MSB of current CRC value = position (8 bits) in lookup table (if we had a lookup table)
        t = s ^ (s >> 4);                           // bit mangling between high and low nibble
        c = (c << 8) ^ t ^ (t << 5) ^ (t << 12);    // xor'ing with polynome 0x1021 (bit positions 0, 5 and 12)
    }
    return c;
}

#endif