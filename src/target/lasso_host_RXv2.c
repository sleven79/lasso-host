/******************************************************************************/
/*                                                                     		  */
/*  PROJECT NAME  : openRIO													  */
/*  FILE          : openRIO_taskSCI7.c                            		 	  */
/*  DATE          : Thu, Aug 4, 2016                                   		  */
/*  DESCRIPTION   : Sample SCI7 communication in FreeRTOS for openRIO	  	  */
/*  CPU TYPE      : RXv2                                                	  */
/*                                                                     		  */
/*  Resources used: SCI7												      */
/*                                                                     		  */
/******************************************************************************/


// Requirements:
// SCI:
// - User must select SCI number (see below)
// - SCI API generated with Renesas code generator (include "r_cg_sci.h")
// - CTS# pin (input) configured by Renesas code generator
// - configure RTS# pin, if used (see below)
// - interrupt priorities of TX (start) and RX configured by code generator
// - interrupt priorities of TEI (transmit end) and ERI (receive error) must
//   be configured below
// - ensure that code generator provides TX/RX/TEI/ERI interrupt handlers
//   and modify them is necessary
// DMA:
// - DMACA FIT module must be activated by call to R_DMACA_Init()
// - can transfer up to 65535 Bytes in one shot

#include "lasso_host.h"

#ifdef INCLUDE_LASSO_HOST

// System includes
#include "oRIOn.h"

// Renesas FIT modules
#include "r_sci_rx_if.h"			// SCI
#include "r_dmaca_rx_if.h"			// DMA


// user configuration
#define LASSO_SCI_CH			7	// select SCI number from 0...7 or 12
#define LASSO_TXI_RXI_INT_PRI  10	// TXI & RXI priorities low=0 ... high=15
#define LASSO_TEI_ERI_INT_PRI	5	// TEI & ERI priorities (BL0 group) low=0 ... high=15
#define LASSO_DMA_CH			0	// select DMA channel for UART TX from 0...7
#define LASSO_DMA_CH_INT_PRI	5	// DMA transfer end priority low=0 ... high=15


// internal use
#define LASSO_SCI 				_LASSO_SCI(LASSO_SCI_CH)		// 1. combine
#define _LASSO_SCI(x)			__LASSO_SCI(x)					// 2. expand x
#define __LASSO_SCI(x)			SCI ## x						// 3. concat

#define LASSO_SCI_IR			_LASSO_SCI_IR(LASSO_SCI_CH)
#define _LASSO_SCI_IR(x)		__LASSO_SCI_IR(x)
#define __LASSO_SCI_IR(x)		IR_SCI ## x ## _TXI ## x

#define LASSO_SCI_TXI_IR		_LASSO_SCI_TXI_IR(LASSO_SCI_CH)
#define _LASSO_SCI_TXI_IR(x)	__LASSO_SCI_TXI_IR(x)
#define __LASSO_SCI_TXI_IR(x)	ICU.IR[IR_SCI ## x ## _TXI ## x].BIT.IR

#define LASSO_SCI_TXI_IEN		_LASSO_SCI_TXI_IEN(LASSO_SCI_CH)
#define _LASSO_SCI_TXI_IEN(x)	__LASSO_SCI_TXI_IEN(x)
#define __LASSO_SCI_TXI_IEN(x)	ICU.IER[IER_SCI ## x ## _TXI ## x].BIT.IEN_SCI ## x ## _TXI ## x

/*
#define LASSO_BL0_TEI   	_LASSO_BL0_TEI(LASSO_SCI_CH)
#define _LASSO_BL0_TEI(x)	__LASSO_BL0_TEI(x)
#define __LASSO_BL0_TEI(x)	BSP_INT_SRC_BL0_SCI ## x ## _TEI ## x

#define LASSO_BL0_ERI   	_LASSO_BL0_ERI(LASSO_SCI_CH)
#define _LASSO_BL0_ERI(x)	__LASSO_BL0_ERI(x)
#define __LASSO_BL0_ERI(x)	BSP_INT_SRC_BL0_SCI ## x ## _ERI ## x

#define R_SCI_Create()		_R_SCI_Create(LASSO_SCI_CH)
#define _R_SCI_Create(x)	__R_SCI_Create(x)
#define __R_SCI_Create(x)	R_SCI ## x ## _Create();

#define R_SCI_Start()		_R_SCI_Start(LASSO_SCI_CH)
#define _R_SCI_Start(x)	    __R_SCI_Start(x)
#define __R_SCI_Start(x)	R_SCI ## x ## _Start();

#define r_sci_tei_int		_r_sci_tei_int(LASSO_SCI_CH)
#define _r_sci_tei_int(x)	    __r_sci_tei_int(x)
#define __r_sci_tei_int(x)	r_sci ## x ## _transmitend_interrupt

#define r_sci_eri_int		_r_sci_eri_int(LASSO_SCI_CH)
#define _r_sci_eri_int(x)	    __r_sci_eri_int(x)
#define __r_sci_eri_int(x)	r_sci ## x ## _receiveerror_interrupt
*/

#define LASSO_DMA 			_LASSO_DMA(LASSO_DMA_CH)
#define _LASSO_DMA(x)		__LASSO_DMA(x)
#define __LASSO_DMA(x)		DMAC ## x

#if (LASSO_SCI_CH == 7)		// FTDI and USB cable
#define LASSO_PIN_CFG { \
							oRIOn_configureGPIO(P90, UART_TX); \
							oRIOn_configureGPIO(P91, DIGITAL_OUT | OUT_LOW);	\
							oRIOn_configureGPIO(P92, UART_RX); \
							oRIOn_configureGPIO(P93, UART_CTS); \
					  }
// Note: RTS# control is manual via GPIO
#elif (LASSO_SCI_CH == 6)	// XBee
#define LASSO_PIN_CFG { \
							oRIOn_configureGPIO(P00, UART_TX); \
							oRIOn_configureGPIO(P02, DIGITAL_OUT | OUT_LOW);	\
							oRIOn_configureGPIO(P01, UART_RX); \
							oRIOn_configureGPIO(PJ3, UART_CTS); \
					  }
#else
	#error "Must implement your own UART pin configuration for lasso."
#endif


/******************************************************************************/
/* Private variables														  */
/******************************************************************************/
static volatile bool sci_dma_tend = true;


/******************************************************************************/
/* Private functions														  */
/******************************************************************************/

// once DMA transfer finishes, ISR must be installed in global vector table
void __attribute__ ((interrupt)) lasso_SCI_DMA_TEND_ISR(void) {
	LASSO_SCI_TXI_IEN = 0;	// DMA is not int destination any more, but CPU is (so disable ints)
	sci_dma_tend = true;
}


/******************************************************************************/
/* Public functions															  */
/******************************************************************************/

// configures UART TX/RX and DMA TX transmission channel
int32_t lasso_comSetup_RXv2(void) {
	sci_cfg_t cfg;
	sci_hdl_t hdl;

	cfg.async.baud_rate = 115200;
	cfg.async.clk_src = SCI_CLK_INT;
	cfg.async.data_size = SCI_DATA_8BIT;
	cfg.async.parity_en = SCI_PARITY_OFF;
	cfg.async.stop_bits = SCI_STOPBITS_1;
	cfg.async.int_priority = LASSO_TXI_RXI_INT_PRI;	// TXI, TEI, RXI, ERI INT priority

	if (R_SCI_Open(LASSO_SCI_CH, SCI_MODE_ASYNC, &cfg, FIT_NO_FUNC, &hdl) != SCI_SUCCESS) {
		while(1);
	}	// Note: R_SCI_Open enables only RXI/ERI ints, sets TXI int flag

	// Pins must be adapted manually for each SCI channel
	LASSO_PIN_CFG;

/*  // register group BL0 interrupts associated with SCI
    R_BSP_InterruptWrite(LASSO_BL0_TEI, (bsp_int_cb_t)r_sci_tei_int);
    R_BSP_InterruptWrite(LASSO_BL0_ERI, (bsp_int_cb_t)r_sci_eri_int);

	// enable group interrupts in order to use transmission completed and reception error interrupts
	bsp_int_ctrl_t int_ctrl;
	int_ctrl.ipl = LASSO_TEI_ERI_INT_PRI;
	R_BSP_InterruptControl(LASSO_BL0_TEI, BSP_INT_CMD_GROUP_INTERRUPT_ENABLE, &int_ctrl);
*/

    // Configure DMA channel used to transmit data back and forth between
	R_DMACA_Init();
    dmaca_return_t ret;
	ret = R_DMACA_Open(LASSO_DMA_CH);
	if (ret != DMACA_SUCCESS) return EACCES;

	dmaca_transfer_data_cfg_t p_data_cfg;
	p_data_cfg.transfer_mode = DMACA_TRANSFER_MODE_NORMAL;			// normal transfer, one data for one request, source incrementing
	p_data_cfg.repeat_block_side = DMACA_REPEAT_BLOCK_DISABLE;		// no repeat area
	p_data_cfg.data_size = DMACA_DATA_SIZE_BYTE;					// transfer in 8-bit units to SCI transmit data register
	p_data_cfg.act_source = LASSO_SCI_IR;							// SCI TX IR as activation source (DMA trigger)
	p_data_cfg.request_source = DMACA_TRANSFER_REQUEST_PERIPHERAL;	// DMA transfer request comes from SCI peripheral
	p_data_cfg.dtie_request = DMACA_TRANSFER_END_INTERRUPT_ENABLE;	// once finished, issue transfer end interrupt
	p_data_cfg.esie_request = DMACA_TRANSFER_ESCAPE_END_INTERRUPT_DISABLE;			// do not issue escape end interrupt
	p_data_cfg.rptie_request = DMACA_REPEAT_SIZE_END_INTERRUPT_DISABLE;				// do not issue escape end interrupt
	p_data_cfg.sarie_request = DMACA_SRC_ADDR_EXT_REP_AREA_OVER_INTERRUPT_DISABLE;	// do not issue escape end interrupt
	p_data_cfg.darie_request = DMACA_DES_ADDR_EXT_REP_AREA_OVER_INTERRUPT_DISABLE;	// do not issue escape end interrupt
	p_data_cfg.src_addr_mode = DMACA_SRC_ADDR_INCR;					// source address increment for SCI TX
	p_data_cfg.src_addr_repeat_area = DMACA_SRC_ADDR_EXT_REP_AREA_NONE;		// no source repeat area
	p_data_cfg.des_addr_mode = DMACA_DES_ADDR_FIXED;				// destination address fixed for SCI TX
	p_data_cfg.des_addr_repeat_area = DMACA_DES_ADDR_EXT_REP_AREA_NONE;		// no destination repeat area
	p_data_cfg.offset_value = 0;									// not relevant
	p_data_cfg.interrupt_sel = DMACA_CLEAR_INTERRUPT_FLAG_BEGINNING_TRANSFER;	// do not bother CPU with SCI transmit buffer empty flag
	p_data_cfg.p_src_addr = (void*)0;								// will be set for each transfer individually
	p_data_cfg.p_des_addr = (void*)&(LASSO_SCI.TDR);				// destination is SCI transmit data register
	p_data_cfg.transfer_count = 0;									// will be set for each transfer individually

	ret = R_DMACA_Create(LASSO_DMA_CH, &p_data_cfg);
	if (ret != DMACA_SUCCESS) return EACCES;

	ret = R_DMACA_Int_Enable(LASSO_DMA_CH, LASSO_DMA_CH_INT_PRI);
	if (ret != DMACA_SUCCESS) return EACCES;

    return 0;
}


int32_t lasso_comCallback_RXv2(uint8_t* src, uint32_t cnt) {
    if (sci_dma_tend) {
    	sci_dma_tend = false;
    	LASSO_DMA.DMSAR = src;
    	LASSO_DMA.DMCRA = (uint16_t)cnt;
    	LASSO_DMA.DMCNT.BIT.DTE = 1;	// enable one shot transfer, DMA now becomes int destination
    	LASSO_SCI_TXI_IEN = 1;			// fires DMA immediately, since TXI IR flag is already set
    	return 0;
    }
    else {
        return EBUSY;
    }
}


// computes CRC-16-CCITT over buffer (polynome coefficients 0x1021/0x11021)
// the running crc in local variable c must be truncated to desired byte-width in each round
uint32_t lasso_crcCallback_RXv2(uint8_t* src, uint32_t cnt) {
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


int32_t lasso_rcvCallback_RXv2(void) {
	// when UART char received, read it into lasso host
	if (LASSO_SCI.SSR.BIT.RDRF) {
		lasso_hostReceiveByte(LASSO_SCI.RDR);
	}

	return 0;
}

#endif

