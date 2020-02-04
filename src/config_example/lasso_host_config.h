/******************************************************************************/
/*                                                                            */
/*  \file       lasso_host_config.h                                           */
/*  \date       Mon, Feb 25, 2019                                             */
/*  \author     Severin Leven                                                 */
/*                                                                            */
/*  \brief      Lasso configurable data server (host) configuration settings  */
/*                                                                            */
/*  !!!         Copy this file to your own project and adapt it          !!!  */
/*                                                                            */
/*  This file is part of Lasso configurable data server. All configuration    */
/*  options for the Lasso host can be found here and are user modifiable.     */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  Target CPU: any 32-bit                                                    */
/*  Ressources: CPU, 1x serial communication channel, 1x periodic timer tick  */
/*                                                                            */
/******************************************************************************/

#ifndef LASSO_HOST_CONFIG_H
#define LASSO_HOST_CONFIG_H

#include "lasso_host.h"

#define INCLUDE_LASSO_HOST


//------------------------------//
// USER SETTINGS FOR LASSO HOST //
//------------------------------//

// Lasso host memory allocator:
// - if malloc() is used, sufficient heap must be allocated on host
#define LASSO_HOST_MALLOC(x)                        malloc(x)

// Lasso host memory access alignment:
// - for optimal performance, Lasso accesses memory as Byte, Word or Longword
// - some processor architectures prohibit unaligned Word and Longword access
// - e.g. unaligned (Byte-aligned) access possible for Renesas RX
// - not possible on ARM Cortex-M0, not recommended on Cortex-M3/M4
// - careful with endianness: host endianness prevails
#define LASSO_HOST_UNALIGNED_MEMORY_ACCESS          (0)

// Lasso host endianness:
// - selectable on Renesas RX
// - little-endian on Cypress PSoC, TI Tiva, TI AM335x
#define LASSO_HOST_LITTLE_ENDIAN                    (1)

// Lasso host signature placed in Flash (0) or SRAM (1)
// - on devices such as PSoC3 and PSoC5, only SRAM should be used because
//   the base addresses for DMA access from Flash and SRAM are not the same
#define LASSO_HOST_SIGNATURE_IN_SRAM                (1)

// Lasso host internal timestamp:
// - counts lasso ticks
// - provides timestamp datacell by default
// - not mandatory (user can implement own timestamp)
#define LASSO_HOST_TIMESTAMP                        (1)

// Lasso host tick period:
// - call period of lasso_hostHandleCOM()
// - specified in [ms], valid values: >0, <250.0 (advertisement period)
// - when transmitting extended frames in COBS mode, mind the following:
//   >> LASSO_HOST_TICK_PERIOD_MS >= 256 * 1000 * 10/ UART_BAUD_RATE
#define LASSO_HOST_TICK_PERIOD_MS                   (10)

// Lasso host ISR period divider:
// - used in lasso scheduler ISR (interrupt service routine)
// - scales down ISR frequency to tick frequency
// - lasso tick frequency = ISR frequency / divider, where divider is integer
// - if divider == 1, there is no computational overhead in ISR
#define LASSO_HOST_ISR_PERIOD_DIVIDER               (10)

// Lasso host frame size:
// - messages are subdivided into frames before transmission
// - for COBS encoding: frame size = 256
// - for RN and ESC encoding: any multiple of 256 (up to 65536)
// - usually, limit is defined by max. DMA transmission size or RAM limit
// - on MAX32630FTHR, the limit is 64 Byte due to the USB bulk endpoint
#define LASSO_HOST_MAX_FRAME_SIZE                   (4096)

// Lasso host CRC Byte-width:
// - width valid for all lasso message types, if enabled further below
// - valid values: 1, 2, 4 (however, only 2 supported so far)
#define LASSO_HOST_CRC_BYTEWIDTH                    (2)

// Lasso host serial port baudrate
// - used internally to calculate response latency
#define LASSO_HOST_BAUDRATE                         (115200)

// Lasso host incoming message (command) encoding
// - RN, ESCS or COBS encoding (unique termination necessary)
// - the same setting is applied to outgoing message (response) encoding
#define LASSO_HOST_COMMAND_ENCODING                 LASSO_ENCODING_RN

// Lasso host incoming message (command) buffer size in [Bytes]
// - min. 16, max. 64 Bytes!
// - careful when sending string data!
#define LASSO_HOST_COMMAND_BUFFER_SIZE              (16)

// Lasso host incoming message (command) CRC
// - for RN encoding: CRC must be disabled
// - for other encodings: if enabled, CRC generator must be provided by user
#define LASSO_HOST_COMMAND_CRC_ENABLE               (0)

// Lasso host incoming message (command) timeout in [ticks]
// - if incoming message incomplete after timeout, receive buffer is reset
// - integer value > 0 required
#define LASSO_HOST_COMMAND_TIMEOUT_TICKS            (5)

// Lasso host outgoing message (strobe) encoding
// - NONE required for RN command encoding
// - either NONE or same as command encoding for ESCS and COBS
#define LASSO_HOST_STROBE_ENCODING                  LASSO_ENCODING_NONE

// Lasso host outgoing message (strobe) dynamics
// - dynamic strobing allows selection of custom update periods for datacells
// - strobe size is adjusted dynamically to active set of datacells
// - STATIC required for strobe encoding "NONE"
// - either STATIC or DYNAMIC for strobe encoding "ESCS" or "COBS"
#define LASSO_HOST_STROBE_DYNAMICS                  LASSO_STROBE_STATIC

// Lasso host outgoing message (strobe) minimum period in [ticks]
// - integer value > 0 required
#define LASSO_HOST_STROBE_PERIOD_MIN_TICKS          (10)

// Lasso host outgoing message (strobe) maximum period in [ticks]
// - integer value < 65535 required
#define LASSO_HOST_STROBE_PERIOD_MAX_TICKS          LASSO_STROBE_SLOWEST

// Lasso host outgoing message (strobe) default period in [ticks]
#define LASSO_HOST_STROBE_PERIOD_TICKS      LASSO_HOST_STROBE_PERIOD_MIN_TICKS

// Lasso host outgoing message (strobe) trigger synchronization
// - strobing can be synchronized on external, user-provided event
#define LASSO_HOST_STROBE_EXTERNAL_SYNC             (0)

// Lasso host outgoing message (strobe) buffer synchronization
// - strobing can be configured for external, user-provided buffer
#define LASSO_HOST_STROBE_EXTERNAL_SOURCE           (0)

// Lasso host outgoing message (strobe) CRC
// - for all encodings: if enabled, CRC generator must be provided by user
#define LASSO_HOST_STROBE_CRC_ENABLE                (1)

// Lasso host outgoing message (response) buffer size in [Bytes]
// - min. 32, max. 256 Bytes
// - careful when sending string data!
#define LASSO_HOST_RESPONSE_BUFFER_SIZE             (96)

// Lasso host outgoing message (response) latency in [ticks]
// - maximum delay before response is sent back to client, <= 250ms required
// - number of ticks from command reception to response sendout
// - e.g. 50ms (if 1 tick == 10ms)
// - during strobing, latency depends on strobe length
// - integer value > 0 required
#define LASSO_HOST_RESPONSE_LATENCY_TICKS           (5)

// Lasso host command and response processing mode
// - choose between LASSO_ASCII_MODE and LASSO_MSGPACK mode and recall:
// - ASCII mandatory for RN encoding
// - any of (ASCII, MSGPACK) for other encodings
#define LASSO_HOST_PROCESSING_MODE                  LASSO_ASCII_MODE

// Lasso host enable notifications?
// - only for full ESCS/COBS modes (command/response & strobe)
// - adopts same encoding scheme as command/response/strobe
// - does not use CRC
// - 1=enable, 0=disable
#define LASSO_HOST_NOTIFICATIONS                    (0)

// Adjust notification size reasonable (max expected debug message length)
#define LASSO_HOST_NOTIFICATION_BUFFER_SIZE         (256)


#ifdef __cplusplus
extern "C" {
#endif

// User-specific functions implemented in lasso_host_yourTarget.c:
// - example for lasso_host_PSoC5.c:
extern int32_t lasso_comSetup_PSoC5(void);
extern int32_t lasso_comCallback_PSoC5(uint8_t* src, uint32_t cnt);
#if (LASSO_HOST_STROBE_CRC_ENABLE == 1) || (LASSO_HOST_COMMAND_CRC_ENABLE == 1)
extern uint32_t lasso_crcCallback_PSoC5(uint8_t* src, uint32_t cnt);
#endif

#ifdef __cplusplus
}
#endif

#endif /* LASSO_HOST_CONFIG_H */
