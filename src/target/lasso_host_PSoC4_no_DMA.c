// -----------------
// Lasso data server
// -----------------
// Host implementation v0.1 - device specifics for Cypress PSoC4 (devices without DMA)
// Note:
// 1) in order to use full floating point support (for printf and scanf), make sure that either
//    - newlib-nano is not the default library to link with, or
//    - newlib-nano float formatting is enabled and additional command line flag "-u _scanf_float" is set, or
//    - two command line flags "-u _printf_float" and "-u _scanf_float" are set
//   (settings to be found in Project->Build settings->ARM GCC x.y-...->Linker, additional Flash usage about 25kB, some extra RAM too)
//
//   For PSoC400S: ROM is limited to 32k, no space to allow for "-u _scanf_float" !!!
//                 This means, no float values can be written by client to lasso host datacells.
//
// 2) Adjust heap size in Design Wide Ressources -> System
//    - info on required/estimated heap size can be found in lasso_host.c
// 3) Strobe size limitation = heap size (usually < 4096 Bytes, e.g. PSoC4000S)
// 4) Requires a UART component called LASSO_UART in .cysch (UART communication port)
// 5) Requires an ISR component called isr_lasso_tx in .cysch (for UART packet transmission)
// 6) Requires a COUNTER component called LASSO_CLK in .cysch (for lasso scheduler ISR calls)
// 7) Requires an ISR component called isr_lasso in .cysch (lasso scheduler ISR)
 

#include "lasso_host.h"

#ifdef INCLUDE_LASSO_HOST

#include <project.h>


/******************************************************************************/
/* Private defines                                                            */
/******************************************************************************/
    
#define UART_FIFO_LEN (16)
    
    
/******************************************************************************/
/* Private variables                                                          */
/******************************************************************************/
    
static volatile uint8_t* srcbuf;
static volatile uint32_t bufcnt = 0;

    
/******************************************************************************/
/* Private function declarations                                              */
/******************************************************************************/
    
CY_ISR(LASSO_ISR) { 
#if (LASSO_HOST_ISR_PERIOD_DIVIDER == 1)    
    static volatile uint32_t scaler = 0;
 
    if (scaler == 0) {
        scaler = LASSO_HOST_ISR_PERIOD_DIVIDER; // divider must be integer !
#else
    {
#endif
        lasso_hostHandleCOM();
#if (LASSO_HOST_ISR_PERIOD_DIVIDER == 1)    
    }
    
    scaler--;
#else
    }
#endif
    LASSO_CLK_ClearInterrupt(LASSO_CLK_INTR_MASK_TC); 
}


CY_ISR(LASSO_TX_ISR) { 
    uint8_t c = LASSO_UART_UART_TX_BUFFER_SIZE;
        
    if (bufcnt < c) {
        c = bufcnt;
    }        
    bufcnt -= c;
    
    while(c--) {
        LASSO_UART_TX_FIFO_WR_REG = *srcbuf++;     // see LASSO_UART_SpiUartWriteTxData()   
    }

    if (bufcnt == 0) {
        isr_lasso_tx_Disable();
    }
    else {   
        LASSO_UART_ClearTxInterruptSource(LASSO_UART_INTR_TX_EMPTY);
    }
}
    

/******************************************************************************/
/* Public function declarations                                               */
/******************************************************************************/

// set up lasso scheduler ISR and communication ISR, UART and ISR clock
int32_t lasso_comSetup_PSoC4_no_DMA(void) {
    isr_lasso_StartEx(LASSO_ISR);           // assign new vector and start ISR
    isr_lasso_ClearPending();

    isr_lasso_tx_StartEx(LASSO_TX_ISR);     // assign new vector but do not start yet    
    isr_lasso_tx_Disable();
    
    LASSO_UART_Start();                   
    LASSO_CLK_Start();    
    
    return 0;
}
    

// configures and triggers transmission ISR on UART, or, if still transmitting, returns busy error code
int32_t lasso_comCallback_PSoC4_no_DMA(uint8_t* src, uint32_t cnt) {
    if (LASSO_UART_SpiUartGetTxBufferSize()) {
        return EBUSY;   
    }
    else {
        srcbuf = src;
        bufcnt = cnt;
        isr_lasso_tx_Enable();  // this will fire the ISR immediately, since UART TX buffer is empty
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