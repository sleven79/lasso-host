// -----------------
// Lasso data server
// -----------------
// Host implementation v0.1 - device specifics for Cypress PSoC6, running on CM0+ Core
// Note: 
// 1) in order to use full floating point support (for printf and scanf), make sure that either
//    - newlib-nano is not the default library to link with, or
//    - newlib-nano float formatting is enabled and additional command line flag "-u _scanf_float" is set, or
//    - two command line flags "-u _printf_float" and "-u _scanf_float" are set
//    (settings to be found in Project->Build settings->ARM GCC x.y-...->Linker, additional Flash usage about 25kB, some extra RAM too)
// 2) Adjust heap size in CM0+ Core's startup file (e.g. "ARM GCC Generic->startup_psoc6_01_cm0plus.S" found in source file tree)
//    - for this, add preprocessor macro "__HEAP_SIZE=#Bytes" in compiler settings for CM0+ Core
//    - info on required/estimated heap size can be found in lasso_host.c
// 3) Strobe size limitation = 65536 Bytes (=max DMA burst length for PSoC6 when using X- and Y-loops)! 

#include "lasso_host.h"

#ifdef INCLUDE_LASSO_HOST

#include <project.h>
#include <stdio.h>      // for using printf(), if desired
#include <stdbool.h>    
    
#if (LASSO_HOST_NOTIFICATION_USE_PRINTF == 1)    
extern volatile bool notification_ready;
#endif
    
//--------------------------//
// Module private functions //
//--------------------------//    

void LASSO_ISR_handler(void) {
#if (LASSO_HOST_ISR_PERIOD_DIVIDER > 1)    
    static uint8_t c = LASSO_HOST_ISR_PERIOD_DIVIDER;
    if (c-- == 0) {
#else
    {
#endif
        lasso_hostHandleCOM();
#if (LASSO_HOST_ISR_PERIOD_DIVIDER > 1)        
        c = LASSO_HOST_ISR_PERIOD_DIVIDER;
#endif    
    }
}    
    
void LASSO_UART_ISR_handler(void) {
#if (LASSO_HOST_NOTIFICATION_USE_PRINTF == 1)    
    notification_ready = lasso_hostSignalFinishedCOM();
#else
    lasso_hostSignalFinishedCOM();    
#endif
}
    
    
//-------------------------//
// Module public functions //
//-------------------------//

// configures DMA channel for transmission on UART (here: SCB5)
int32_t lasso_comSetup_PSoC6(void)
{
    // start UART port
    /*
    if (CY_SCB_UART_SUCCESS != LASSO_UART_Init(&LASSO_UART_config)) {
        while(1); // Handle possible errors
    }
    LASSO_UART_Enable();
    */   
    LASSO_UART_Start();    
    
    // start DMA hardware, but leave channel disabled for now
    LASSO_DMA_Init(); 
    
    LASSO_DMA_SetDescriptorType(&LASSO_DMA_Descriptor_1, CY_DMA_2D_TRANSFER);    
    LASSO_DMA_SetDstAddress(&LASSO_DMA_Descriptor_1, (const void*)CYREG_SCB5_TX_FIFO_WR);
    LASSO_DMA_SetXloopSrcIncrement(&LASSO_DMA_Descriptor_1, 1);
    LASSO_DMA_SetXloopDstIncrement(&LASSO_DMA_Descriptor_1, 0);
    LASSO_DMA_SetYloopSrcIncrement(&LASSO_DMA_Descriptor_1, 1);
    LASSO_DMA_SetYloopDstIncrement(&LASSO_DMA_Descriptor_1, 0);
    
    LASSO_DMA_SetDescriptorType(&LASSO_DMA_Descriptor_2, CY_DMA_1D_TRANSFER);
    LASSO_DMA_SetDstAddress(&LASSO_DMA_Descriptor_2, (const void*)CYREG_SCB5_TX_FIFO_WR);
    LASSO_DMA_SetXloopSrcIncrement(&LASSO_DMA_Descriptor_2, 1);
    LASSO_DMA_SetXloopDstIncrement(&LASSO_DMA_Descriptor_2, 0);    
    
    // start ISR for periodic communication
    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&LASSO_ISR_cfg, &LASSO_ISR_handler)) {
        while(1); // Handle possible errors
    }
    NVIC_ClearPendingIRQ(LASSO_ISR_cfg.intrSrc);
    NVIC_EnableIRQ(LASSO_ISR_cfg.intrSrc);     
    
    // start ISR for DMA transmission end
    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&LASSO_UART_ISR_cfg, &LASSO_UART_ISR_handler)) {
        while(1); // Handle possible errors
    }
    NVIC_ClearPendingIRQ(LASSO_UART_ISR_cfg.intrSrc);
    NVIC_EnableIRQ(LASSO_UART_ISR_cfg.intrSrc);    
    
    return 0;
}
    

// configures and triggers DMA transmission on UART, or, if still transmitting, returns busy error code
// note that cnt <= 65536 !
int32_t lasso_comCallback_PSoC6(uint8_t* src, uint32_t cnt)    
{
    uint8_t remainder;
    
    //static uint8_t c = 0;
    
    if (LASSO_UART_IsTxComplete()) {
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
        
        // set source address (destination address is already set)        
        LASSO_DMA_SetSrcAddress(&LASSO_DMA_Descriptor_1, (const void*)src);
        
        // configure X- and Y-loop transfer numbers
        if (cnt > 256) {
            // descriptor 1 is used for entire multiples of 256 Bytes
            LASSO_DMA_SetXloopDataCount(&LASSO_DMA_Descriptor_1, 256);
            LASSO_DMA_SetYloopDataCount(&LASSO_DMA_Descriptor_1, cnt >> 8);            
            LASSO_DMA_SetYloopSrcIncrement(&LASSO_DMA_Descriptor_1, 256);                      
            
            // descriptor 2 is used for the remainder
            remainder = cnt % 256;
            if (remainder) {
                LASSO_DMA_SetNextDescriptor(&LASSO_DMA_Descriptor_2);
                LASSO_DMA_SetChannelState(&LASSO_DMA_Descriptor_1, CY_DMA_CHANNEL_ENABLED);

                LASSO_DMA_SetSrcAddress(&LASSO_DMA_Descriptor_2, (const void*)(src + cnt - remainder));
                LASSO_DMA_SetXloopDataCount(&LASSO_DMA_Descriptor_2, remainder);     
            }
            else {
                LASSO_DMA_SetNextDescriptor(&LASSO_DMA_Descriptor_1);
                LASSO_DMA_SetChannelState(&LASSO_DMA_Descriptor_1, CY_DMA_CHANNEL_DISABLED);                
            }
        }
        else {
            LASSO_DMA_SetXloopDataCount(&LASSO_DMA_Descriptor_1, cnt);            
            LASSO_DMA_SetYloopDataCount(&LASSO_DMA_Descriptor_1, 1);
            LASSO_DMA_SetNextDescriptor(&LASSO_DMA_Descriptor_1);
            LASSO_DMA_SetChannelState(&LASSO_DMA_Descriptor_1, CY_DMA_CHANNEL_DISABLED);
        }            
        
        // enabling channel triggers transfer to UART immediately,
        // channel gets disabled automatically when done
        LASSO_DMA_ChannelEnable();
    }
    else {
        return EBUSY;   
    }
            
    return 0;
}


// computes CRC-16-CCITT over buffer (polynome coefficients 0x1021/0x11021)
// the running crc in local variable c must be truncated to desired byte-width in each round
uint32_t lasso_crcCallback_PSoC6(uint8_t* src, uint32_t cnt) {
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