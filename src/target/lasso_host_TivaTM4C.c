// -----------------
// Lasso data server
// -----------------
// Host implementation v0.1 - device specifics for TI Tiva TM4C
// Note: apparently, CCS enables full floating point support (for printf and scanf) by default
// Note: adjust heap and stack sizes -> in tm4c123gh6pm.cmd (Linker command file), e.g.
// --heap_size=4096                                                            
// --stack_size=2048
// Note: strobe size limitation = 1024 Bytes !

#include "lasso_host.h"

#ifdef INCLUDE_LASSO_HOST

// ------------------------------------------------- //
// Modify to adapt to a specific Tiva TM4C processor //
// ------------------------------------------------- //
#define TARGET_IS_TM4C123_RB2

#include <errno.h>

#include "inc/hw_memmap.h"
#include "inc/hw_uart.h"
#include "driverlib/rom.h"
#include "driverlib/udma.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/uart.h"
    
// Allocate the uDMA channel control table.
// NOTE: Table must be 1024-byte aligned.
// NOTE: Example is a full table for all modes and channels.
#pragma DATA_ALIGN(DMAControlTable, 1024)
static uint8_t DMAControlTable[1024];    

static void InitUART0(void) {
    // enable GPIOA and wait for peripheral to be ready
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    while(!ROM_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA));   
    
    // configure 2 UART pins
    ROM_GPIOPinConfigure(GPIO_PA0_U0RX);
    ROM_GPIOPinConfigure(GPIO_PA1_U0TX);
    ROM_GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);    
    
    // enable UART0 and wait for peripheral to be ready
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    while(!ROM_SysCtlPeripheralReady(SYSCTL_PERIPH_UART0));    
    
    // set UART clock, 8 data bits, 1 stop bit, no parity bit
    // no FIFOs
    ROM_UARTConfigSetExpClk(UART0_BASE, ROM_SysCtlClockGet(), 115200,
                            UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                            UART_CONFIG_PAR_NONE);
    
    // configure EOT bit in UARTCTL (trigger DMA as soon as Tx idle)
    //ROM_UARTTxIntModeSet(UART0_BASE, UART_TXINT_MODE_EOT);
    
    // enable UART0TX for DMA link
    ROM_UARTDMAEnable(UART0_BASE, UART_DMA_TX);
    
    // UART0 interrupts not required
    //ROM_IntEnable(INT_UART0);
    
    // enable peripheral -> already performed with ROM_UARTConfigSetExpClk()
    //ROM_UARTEnable(UART0_BASE);
}

static void InituDMA(void) {
    // enable uDMA and wait for peripheral to be ready
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
    while(!ROM_SysCtlPeripheralReady(SYSCTL_PERIPH_UDMA));
    
    // Enable the uDMA controller.
    ROM_uDMAEnable();
    
    // Set the base for the channel control table.
    ROM_uDMAControlBaseSet(DMAControlTable);
    
    // No attributes must be set for a software-based transfer. The attributes
    // are cleared by default, but are explicitly cleared here, in case they
    // were set elsewhere.
    ROM_uDMAChannelAttributeDisable(UDMA_CHANNEL_UART0TX, UDMA_ATTR_USEBURST |
                             UDMA_ATTR_ALTSELECT |
                             UDMA_ATTR_HIGH_PRIORITY |
                             UDMA_ATTR_REQMASK);
    
    // Now set up the characteristics of the transfer for 8-bit data size, with
    // source and destination increments in bytes, and a byte-wise buffer copy.
    // A bus arbitration size of 8 is used.
    ROM_uDMAChannelControlSet(UDMA_CHANNEL_UART0TX | UDMA_PRI_SELECT,
                              UDMA_SIZE_8 |
                              UDMA_SRC_INC_8 | UDMA_DST_INC_NONE |
                              UDMA_ARB_8);
    
    // Transfer buffers and size are not configured here.
    // Refer to SysTickISRHandler()
    
    // The channel is not enabled here since transfer would begin immediately.
    // Refer to SysTickISRHandler()
}


//------------------//
// Module functions //
//------------------//

// configures DMA channel for transmission on UART
int32_t lasso_comSetup_TivaTM4C (void) {
    InitUART0();
    InituDMA();
    
    return 0;
}


// configures and triggers DMA transmission on UART, or, if still transmitting, returns busy error code
int32_t lasso_comCallback_TivaTM4C(uint8_t* src, uint32_t cnt) {
    if (ROM_UARTBusy(UART0_BASE)) {
        return EBUSY;   
    }
    else {
        // The transfer buffers and transfer size are now configured. The transfer
        // uses AUTO mode, which means that the transfer automatically runs to
        // completion after the first request.
        ROM_uDMAChannelTransferSet(UDMA_CHANNEL_UART0TX | UDMA_PRI_SELECT,
                                   UDMA_MODE_BASIC,
                                   src,
                                   (void *)(UART0_BASE + UART_O_DR),
                                   cnt);
        
        // enable channel        
        ROM_uDMAChannelEnable(UDMA_CHANNEL_UART0TX);    
    }
    return 0;
}


// computes CRC-16-CCITT over buffer (polynome coefficients 0x1021/0x11021)
// the running crc in local variable c must be truncated to desired byte-width in each round
uint32_t lasso_crcCallback_TivaTM4C(uint8_t* src, uint32_t cnt) {
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
