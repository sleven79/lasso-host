/******************************************************************************/
/*                                                                            */
/*  \file       lasso_host.c                                                  */
/*  \date       Jan 2017 - Feb 2019                                           */
/*  \author     Severin Leven                                                 */
/*                                                                            */
/*  \brief      Lasso host (data server) library                              */
/*                                                                            */
/*  This file is part of the Lasso host library. Lasso is a configurable and  */
/*  efficient mechanism for data transfer between a host (server) and client. */
/*                                                                            */
/*  All private and public API definitions, typedefs, variables, structs and  */
/*  functions, mandatory for proper operation of Lasso are collected here.    */
/*                                                                            */
/*  Optional functionalities are in separate files, e.g. msgpack.c, cobs.c    */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  Target CPU: any 32-bit                                                    */
/*  Ressources: CPU (for com/timer ressources see target-specific module)     */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  Lasso host                                                                */
/*  - makes datacells available to remote client through a serial link        */
/*  - provides efficient real-time periodic transmission of datacells values  */
/*  - provides an interface to interprete commands sent by client:            */
/*      - to configure data space                                             */
/*      - to configure update rate                                            */
/*      - to write into data cells                                            */
/*  - a special real-time periodic command mode is possible (R/C mode)        */
/*  - relies on an external, target-specific communication ressource          */
/*  - provides strategies to avoid loss of synchronization & data integrity:  */
/*      - different serialization modes/encodings (ascii, msgpack)            */
/*      - different escaping strategies (RN, ESCS, COBS)                      */
/*      - addition of cyclic redundancy check (CRC)                           */
/*  - must be hooked onto user-supplied, periodic timing ressource            */
/*  - is written in C and relies on standard C libraries (e.g. newlib)        */
/*  - is compatible with embedded uC and uP targets                           */
/*  - provides various hooks for customization (COM, CRC, user callbacks)     */
/*                                                                            */
/*  Lasso client                                                              */
/*  - discovers data cells that host has to offer (data space)                */
/*  - interacts (asynchronously) with host through set of commands            */
/*  - configures desired data space, update rate and writes to data cells     */
/*  - receives strobed bulk data and displays or logs it                      */
/*  - typical target is a PC or tablet computer with a serial link            */
/*                                                                            */
/*  Lasso philosophy                                                          */
/*  Lasso provides bi-directional, configurable data transfers between a host */
/*  (server) and a client, with a priority on data download from host to      */
/*  client. The server would typically be an embedded uC-based system without */
/*  a direct means to display embedded data in real-time. The client would    */
/*  typically be a remote PC (with lasso client side interface) that displays */
/*  and logs the embedded data for immediate or offline inspection.           */
/*  Data downloads are organized as bulk transfers = a snapshot of the        */
/*  selected data space (strobe) is transfered at a specific rate.            */
/*  The transfer rate is identical for all data cells in the selected data    */
/*  space. The transfer rate is derived from the system rate (user-supplied   */
/*  timer ressource) by an integer divider (1,2, ...). A unique time stamp    */
/*  can be added to each bulk transfer.                                       */
/*  Lasso sets a priority on timeliness and precision of update rate rather   */
/*  than on transmission completeness. There is no mechanism to prevent loss  */
/*  of bulk data (e.g. retransmission). This is often handled by lower layers */
/*  in today's communication stacks. However, the loss of synchronization is  */
/*  prevented and loss of bulk data detected with the help of timestamps.     */
/*                                                                            */
/*  Definitions:                                                              */
/*  - Data cell (DC)           = data structure that links to an underlying   */
/*                               memory cell of specific size and             */
/*                               interpretation on the host (RAM, ROM)        */
/*  - Data space (DS)          = entire set of DCs                            */
/*  - Active data space (ADS)  = set of download-enabled (active) DCs         */
/*  - Strobe (STR)             = one bulk transfer (download) of the active DS*/
/*  - Download (DL)            = data transfer from host to client            */
/*  - Upload (UL)              = data transfer from client to host            */
/*  - System update rate (SR)  = maximum rate at which DCs fetch data from    */
/*                               their underlying memory cells (the actual    */
/*                               memory cell's update rate mayby be slower)   */
/*  - Cell update rate (CUR)   = update rate of the underlying memory cell of */
/*                               an individual DC                             */
/*  - Strobe rate (BR)         = bulk download rate (integer divider of SR)   */
/*  - Time stamp (TS)          = unique code incrementing at SR               */
/*                                                                            */
/*  Each data cell associates its underlying memory cell with (host-defined): */
/*  - a type                   = char, int, uint, float type                  */
/*                               and Byte-width information                   */
/*  - a count                  = array size (number of "type" in memory cell) */
/*  - an ASCII name string     = a unique identifier, e.g. "my_variable"      */
/*  - an ASCII unit string     = e.g. "rad/s" or "m/s"                        */
/*  - a cell update rate       = information about memory cell update rate    */
/*                                                                            */
/*  Heap requirements                                                         */
/*  - N dataCell structs (28 Bytes each, 32 on non-packed systems)            */
/*  - 2 dataFrame structs (24 Bytes each, 32 on non-packed systems)           */
/*  - strobe buffer (depends on size of memory cells linked to DCs)           */
/*  - some overhead for msgpack, CRC, RN, ESC, COBS coding (max. 16 Bytes)    */
/*  - receive (incoming) buffer size                                          */
/*  - response (outgoing) buffer size                                         */
/*                                                                            */
/*  - Example: N = 4, e.g. 2x float, 1x 100*uint8, 1x char[10]                */
/*         ->: 128 Bytes max for dataCell structs                             */
/*              64 Bytes max for dataFrame structs                            */
/*             118 Bytes for strobe buffer (32-bit system)                    */
/*              16 Bytes for strobe buffer overhead                           */
/*              32 Bytes for receive buffer (example)                         */
/*              96 Bytes for response buffer (example)                        */
/*           = 454 Bytes                                                      */
/*                                                                            */
/******************************************************************************/


//----------//
// Includes //
//----------//

#include "lasso_host.h"
#include "lasso_host_ver.h"

#ifdef INCLUDE_LASSO_HOST       // refer to "lasso_host_config.h"

#if (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
    #include <stdio.h>
#endif

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
    #include "msgpack.h"
#endif

#if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_COBS)
    #include "cobs.h"
#endif

#if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_ESCS)
    #include "escs.h"
#endif


//---------//
// Defines //
//---------//

// Lasso host parameters
#ifndef LASSO_HOST_PROTOCOL_VERSION     // usually pulled from "lasso_version.h"
#define LASSO_HOST_PROTOCOL_VERSION         0.0     //!< host protocol version
#endif

// Lasso host input commands (valid commands are in ASCII space 0...127)
#define LASSO_HOST_SET_ADVERTISE            'A'     //!< enable advertising
#define LASSO_HOST_SET_STROBE_PERIOD        'P'     //!< set strobe period
#define LASSO_HOST_SET_DATACELL_STROBE      'S'     //!< set data cell strobe
#define LASSO_HOST_SET_DATACELL_VALUE       'V'     //!< set value of data cell
#define LASSO_HOST_SET_DATASPACE_STROBE     'W'     //!< set data space strobe

#define LASSO_HOST_GET_PROTOCOL_INFO        'i'     //!< get protocol info
#define LASSO_HOST_GET_TIMING_INFO          't'     //!< get timing info
#define LASSO_HOST_GET_DATACELL_COUNT       'n'     //!< get # of data cells
#define LASSO_HOST_GET_DATACELL_PARAMS      'p'     //!< get data cell params
#define LASSO_HOST_GET_DATACELL_VALUE       'v'     //!< get data cell value

#define LASSO_HOST_SET_CONTROLS             (0xC1)  //<! R/C mode controls
#define LASSO_HOST_INVALID_MSGPACK_CODE     (0xC1)  //<! ESCS/COBS interleave

// Lasso data cell types
#define LASSO_DATACELL_BYTEWIDTH_1          (0x0000)
#define LASSO_DATACELL_BYTEWIDTH_2          (0x0002)
#define LASSO_DATACELL_BYTEWIDTH_4          (0x0004)
#define LASSO_DATACELL_BYTEWIDTH_5          (0x0008)

#define LASSO_DATACELL_ENABLE_MASK          (0x0001)
#define LASSO_DATACELL_DISABLE_MASK         (0xFFFE)
#define LASSO_DATACELL_BYTEWIDTH_MASK       (0x000E)
#define LASSO_DATACELL_TYPE_MASK            (0x00F0)
#define LASSO_DATACELL_TYPE_SHIFT           (4)
#define LASSO_DATACELL_TYPE_BYTEWIDTH_MASK  (LASSO_DATACELL_TYPE_MASK | \
                                             LASSO_DATACELL_BYTEWIDTH_MASK)

// General availability checks
#ifndef LASSO_HOST_COMMAND_ENCODING
    #define LASSO_HOST_COMMAND_ENCODING LASSO_ENCODING_RN
#endif


// Lasso host memory alignment policy
#ifndef LASSO_MEMORY_ALIGN
    #define LASSO_MEMORY_ALIGN          (4)         //<! Byte boundary alignment
#endif

// Lasso host incoming (command) and outgoing (response) buffer sizes
#ifndef LASSO_HOST_COMMAND_BUFFER_SIZE
    #define LASSO_HOST_COMMAND_BUFFER_SIZE      (64)
#else
    #if (LASSO_HOST_COMMAND_BUFFER_SIZE < 16)
        #error Minimum for LASSO_HOST_COMMAND_BUFFER_SIZE is 16
    #endif
    #if (LASSO_HOST_COMMAND_BUFFER_SIZE > 64)
        #error Maximum for LASSO_HOST_COMMAND_BUFFER_SIZE is 64
    #endif
#endif

#ifndef LASSO_HOST_RESPONSE_BUFFER_SIZE
    #define LASSO_HOST_RESPONSE_BUFFER_SIZE     (96)
#else
    #if (LASSO_HOST_RESPONSE_BUFFER_SIZE < 32)
        #error Minimum for LASSO_HOST_RESPONSE_BUFFER_SIZE is 32
    #endif
    #if (LASSO_HOST_RESPONSE_BUFFER_SIZE > 256)
        #error Maximum for LASSO_HOST_RESPONSE_BUFFER_SIZE is 256
    #endif
#endif

// Lasso host incoming (command) and outgoing (strobe, response) timings
#ifndef LASSO_HOST_COMMAND_TIMEOUT_TICKS
    #define LASSO_HOST_COMMAND_TIMEOUT_TICKS    (5)
#else
    #if (LASSO_HOST_COMMAND_TIMEOUT_TICKS < 1)
        #error Minimum for LASSO_HOST_COMMAND_TIMEOUT_TICKS is 1
    #endif
#endif

#if (LASSO_HOST_STROBE_ENCODING != LASSO_ENCODING_NONE)
    #if (LASSO_HOST_STROBE_ENCODING != LASSO_HOST_COMMAND_ENCODING)
        #error LASSO_HOST_STROBE_ENCODING must match LASSO_HOST_COMMAND_ENCODING if not NONE
    #endif
#endif

#if (LASSO_HOST_STROBE_DYNAMICS == LASSO_STROBE_DYNAMIC)
    #if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_NONE)
        #error LASSO_HOST_STROBE_ENCODING must not be NONE when selecting dynamic strobing
    #endif
#endif

#ifndef LASSO_HOST_STROBE_PERIOD_MIN_TICKS
    #define LASSO_HOST_STROBE_PERIOD_MIN_TICKS  (10)
#else
    #if (LASSO_HOST_STROBE_PERIOD_MIN_TICKS < 1)
        #error Minimum for LASSO_HOST_STROBE_PERIOD_MIN_TICKS is 1
    #endif
#endif

#ifndef LASSO_HOST_STROBE_PERIOD_MAX_TICKS
    #define LASSO_HOST_STROBE_PERIOD_MAX_TICKS  (65535)
#else
    #if (LASSO_HOST_STROBE_PERIOD_MAX_TICKS > 65535)
        #error Maximum for LASSO_HOST_STROBE_PERIOD_MIN_TICKS is 65535
    #endif
#endif

#ifndef LASSO_HOST_STROBE_PERIOD_TICKS
    #define LASSO_HOST_STROBE_PERIOD_TICKS  LASSO_HOST_STROBE_PERIOD_MIN_TICKS
#else
    #if (LASSO_HOST_STROBE_PERIOD_TICKS < LASSO_HOST_STROBE_PERIOD_MIN_TICKS)
        #error LASSO_HOST_STROBE_PERIOD_TICKS must be >= LASSO_HOST_STROBE_PERIOD_MIN_TICKS
    #elif (LASSO_HOST_STROBE_PERIOD_TICKS > LASSO_HOST_STROBE_PERIOD_MAX_TICKS)
        #error LASSO_HOST_STROBE_PERIOD_TICKS must be <= LASSO_HOST_STROBE_PERIOD_MAX_TICKS
    #endif
#endif

#ifndef LASSO_HOST_RESPONSE_LATENCY_TICKS
    #define LASSO_HOST_RESPONSE_LATENCY_TICKS  (1)
#else
    #if (LASSO_HOST_RESPONSE_LATENCY_TICKS < 1)
        #error Minimum for LASSO_HOST_RESPONSE_LATENCY_TICKS is 1
    #endif
#endif

// convenience function to transform x into string value
#define _TOSTR(x) #x
#define TOSTR(x) _TOSTR(x)

// Lasso host advertising period in [ms]
#define LASSO_HOST_ADVERTISE_PERIOD_MS      (250)

// Lasso host advertising period in [ticks], conversion from [ms]
#define LASSO_HOST_ADVERTISE_PERIOD_TICKS   (LASSO_HOST_ADVERTISE_PERIOD_MS/LASSO_HOST_TICK_PERIOD_MS)

// Lasso host round-trip (command to response) latency in [ticks]
// - maximum theoretical round-trip delay from command to response, based on:
//   1) serial transmission of command from client to host @ baudrate
//   2) n tick(s) maximum reaction delay on host from reception to treatment,
//      where n = LASSO_HOST_RESPONSE_LATENCY_TICKS
//   3) 1 tick maximum treatment delay on host
//   4) 1 tick for rounding-up (C-preprocessor will floor value into a uint16_t)
//   4) serial transmission of response from host to client @ baudrate
//
// - it is assumed that strobing is off (otherwise, maximum latency is
//   approximated as minimum strobe period + roundtrip latency)
//
// - max. roundtrip latency shall be 250ms
//   -> the calculated value can be checked in Lasso client -> Host parameters
#define LASSO_HOST_ROUNDTRIP_LATENCY_TICKS  (((LASSO_HOST_COMMAND_BUFFER_SIZE + \
                                               LASSO_HOST_RESPONSE_BUFFER_SIZE) * 10 * 1000) \
                                              /LASSO_HOST_BAUDRATE/LASSO_HOST_TICK_PERIOD_MS + \
                                               LASSO_HOST_RESPONSE_LATENCY_TICKS + 2)

#if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_RN)
    #if (LASSO_HOST_COMMAND_CRC_ENABLE != 0)
        #error In RN command/response encoding, LASSO_HOST_COMMAND_CRC_ENABLE must be 0
    #endif

    #if (LASSO_HOST_STROBE_ENCODING != LASSO_ENCODING_NONE)
        #error In RN command/response encoding, LASSO_HOST_STROBE_ENCODING must be LASSO_ENCODING_NONE
    #endif

    #if (LASSO_HOST_PROCESSING_MODE != LASSO_ASCII_MODE)
        #error In RN command/response encoding, LASSO_HOST_PROCESSING_MODE must be LASSO_ASCII_MODE
    #endif
#endif


//--------------------//
// Private Prototypes //
//--------------------//

#if (LASSO_HOST_COMMAND_CRC_ENABLE == 1) || (LASSO_HOST_STROBE_CRC_ENABLE == 1)
static uint32_t lasso_crcDefaultCallback (
    uint8_t* buffer,                        //!< buffer start pointer
    uint32_t cnt                            //!< number of Bytes to iterate over
);
#endif


//------------------//
// Private Typedefs //
//------------------//

// 28 Bytes total (packed), up to 32 Bytes total (on unpacked 32-bit system)
typedef struct DATACELL
#if (LASSO_HOST_UNALIGNED_MEMORY_ACCESS == 1)
    __attribute__((packed))
#endif
{
    uint16_t type;              //!< see lasso_host.h for content
    uint16_t count;             //!< data cell can be array of above atomic type
    const void* ptr;            //!< pointer to underlying memory cell
    const char* name;           //!< data cell name string
    const char* unit;           //!< data cell unit string
    lasso_chgCallback onChange; //!< callback for change event
    uint32_t update_rate;       //!< update rate of underlying memory cell (16/16 bits)
    struct DATACELL* next;      //!< singly-linked list
} dataCell;

// 24 Bytes total (packed), up to 32 Bytes total (on unpacked 32-bit system)
typedef struct DATAFRAME
#if (LASSO_HOST_UNALIGNED_MEMORY_ACCESS == 1)
    __attribute__((packed))
#endif
{
    uint16_t countdown;         //!< period countdown
    uint8_t  COBS_backup;       //!< COBS backup Byte (only for COBS encoding)
    uint8_t  valid;             //!< strobe: transmission within one lasso cycle
                                //!< response frame: valid request received
    uint8_t* buffer;            //!< buffer pointer
    uint8_t* frame;             //!< frame pointer (progressive parts in buffer)
    uint32_t Byte_count;        //!< number of Bytes remaining to be transmitted
    uint32_t Bytes_max;         //!< maximum of Bytes allowed in buffer any time
    uint32_t Bytes_total;       //!< current number of Bytes in buffer
} dataFrame;


//-------------------//
// Private Variables //
//-------------------//

static uint8_t   dataCellCount = 0;         //!< number of registered DCs
static dataCell* dataCellFirst = NULL;      //!< pointer to first DC structure
static dataCell* dataCellLast = NULL;       //!< pointer to last DC structure
#if (LASSO_HOST_STROBE_DYNAMICS == LASSO_STROBE_DYNAMIC)
    static uint8_t dataCellMaskBytes = 0;   //!< mask Bytes for strobe dynamics
#endif

static uint8_t*  receiveBuffer = NULL;      //!< buffer for incoming commands
static uint8_t   receiveBufferIndex = 0;    //!< write index in receiveBuffer
static uint32_t  receiveTimeout = 0;        //!< receive timeout counter

static bool      lasso_strobing = false;    //!< global strobe enable flag
static bool      lasso_advertise = true;    //!< advertise unless client connected

// user-supplied callbacks
static lasso_comCallback comCallback = NULL;    //!< trigger communication
#if (LASSO_HOST_COMMAND_CRC_ENABLE == 1) || (LASSO_HOST_STROBE_CRC_ENABLE == 1)
    static lasso_crcCallback crcCallback = lasso_crcDefaultCallback;    //!< CRC
#endif
static lasso_actCallback actCallback = NULL;    //!< strobe de-/activation
static lasso_perCallback perCallback = NULL;    //!< strobe period changed
static lasso_ctlCallback ctlCallback = NULL;    //!< controls changed

static dataFrame strobe = \
    { LASSO_HOST_STROBE_PERIOD_TICKS, 0, true , NULL, NULL, 0, 0, 0};

static dataFrame response = \
    { LASSO_HOST_ROUNDTRIP_LATENCY_TICKS, 0, false, NULL, NULL, 0, \
        LASSO_HOST_RESPONSE_BUFFER_SIZE, 0};

static uint16_t lasso_strobe_period = LASSO_HOST_STROBE_PERIOD_TICKS;
//!< at each expiration, strobe period is reloaded from here

static uint16_t lasso_tick_period = LASSO_HOST_TICK_PERIOD_MS;
//!< tick period can programmatically be changed at run-time


static uint16_t lasso_roundtrip_latency_ticks = LASSO_HOST_ROUNDTRIP_LATENCY_TICKS;

static uint16_t lasso_advertise_period_ticks = LASSO_HOST_ADVERTISE_PERIOD_TICKS;

static uint32_t lasso_overdrive = 0;
//!< non-zero indicates that strobe volume and rate are incompatible


//---------------//
// Protocol info //
//---------------//

// 32-bit value:
// bits 0-1     command encoding (RN, COBS, ESCS)
// bit 2        strobe encoding == command encoding?
// bit 3        processing mode (ASCII, MSGPACK)
// bit 4        strobe dynamics (STATIC, DYNAMIC)
// bits 5-6     CRC Byte width (1,2,3,4)
// bit 7        command CRC enable (YES, NO)
// bit 8        strobe CRC enable (YES, NO)
// bit 9        little endian strobe data (YES, NO)
// bits 10-15   command (receive) buffer size (64 Bytes max)
// bits 16-23   response buffer size (256 Bytes max), some overhead may apply, see lasso_hostRegisterMEM()
// bits 24-31   frame size (256 Bytes min, 65536 Bytes max, steps of 256), not to be confused with strobe size

#define LASSO_PROTOCOL_INFO (((uint32_t)LASSO_HOST_COMMAND_ENCODING) \
+ ((uint32_t)(LASSO_HOST_COMMAND_ENCODING == LASSO_HOST_STROBE_ENCODING) << 2) \
    + ((uint32_t)LASSO_HOST_PROCESSING_MODE << 3) \
    + ((uint32_t)LASSO_HOST_STROBE_DYNAMICS << 4) \
    + (((uint32_t)LASSO_HOST_CRC_BYTEWIDTH - 1) << 5) \
    + ((uint32_t)LASSO_HOST_COMMAND_CRC_ENABLE << 7) \
    + ((uint32_t)LASSO_HOST_STROBE_CRC_ENABLE << 8) \
    + ((uint32_t)LASSO_HOST_LITTLE_ENDIAN << 9) \
    + (((uint32_t)LASSO_HOST_COMMAND_BUFFER_SIZE - 1) << 10) \
    + (((uint32_t)LASSO_HOST_RESPONSE_BUFFER_SIZE - 1) << 16) \
    + ((((uint32_t)LASSO_HOST_MAX_FRAME_SIZE >> 8) - 1) << 24))

static uint32_t lasso_protocol_info = LASSO_PROTOCOL_INFO;

// 16 character signature that a lasso host strobes (advertises)
// when not connected to lasso client
#if (LASSO_HOST_SIGNATURE_IN_SRAM == 1) // place signature in SRAM
static struct __attribute__((packed)) {
#else                                   // place signature in Flash
static const struct __attribute__((packed)) {
#endif
    const char head[10];                //!< ASCII header (lasso id)
    uint32_t info;                      //!< non-ASCII protocol info
    const char tail[2];                 //!< ASCII tail (CR/NL)
} lasso_signature = {{'l', 'a', 's', 's', 'o', 'H', 'o', 's', 't', '/'}, LASSO_PROTOCOL_INFO, {'\r', '\n'}};
// note: trailing \0 for strings is dropped

#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
    #define VERSION_STR(x, y) #x ## #y
static struct __attribute__((packed)) {
    const char head[1];
    const char body[];
} lasso_version = {{'v'}, {TOSTR(LASSO_HOST_PROTOCOL_VERSION)}};
#endif

#if (LASSO_HOST_TIMESTAMP == 1)
    static uint32_t lasso_timestamp = 0;
#endif

//-------------------//
// Private functions //
//-------------------//

/*!
 *  \brief  Default CRC caller function.
 *
 *          In case user forgets to register own CRC callback, this function
 *          provides a sample CRC calculation based on 8-bit XORing.
 *
 *  \return 32-bit CRC value
 */
#if (LASSO_HOST_COMMAND_CRC_ENABLE == 1) || (LASSO_HOST_STROBE_CRC_ENABLE == 1)
static uint32_t lasso_crcDefaultCallback (
    uint8_t* buffer,                        //!< buffer start pointer
    uint32_t cnt                            //!< number of Bytes to iterate over
) {
    uint8_t CRC_value = 0;

    while (cnt--) {
        CRC_value ^= *buffer++;
    }

    return (uint32_t)CRC_value;
}
#endif


/*!
 *  \brief  Append CRC to end of response or strobe frame.
 *
 *  \return Void
 */
#if (LASSO_HOST_COMMAND_CRC_ENABLE == 1) || (LASSO_HOST_STROBE_CRC_ENABLE == 1)
static void lasso_hostAppendCRC (
    uint8_t* buffer,                        //!< buffer start pointer
    uint32_t cnt                            //!< number of Bytes to iterate over
) {
    uint32_t crc = crcCallback(buffer, cnt);
    buffer += cnt;

#if (LASSO_HOST_CRC_BYTEWIDTH == 1)
    *buffer = (uint8_t)crc;
#elif (LASSO_HOST_CRC_BYTEWIDTH == 2)
    #if (LASSO_HOST_UNALIGNED_MEMORY_ACCESS == 1)
        *(uint16_t*)buffer = (uint16_t)crc;
    #else
        uint8_t* crcp = (uint8_t*)&crc;
        cnt = 2;
        while (cnt--) {
            *buffer++ = *crcp++;
        }
    #endif
#elif (LASSO_HOST_CRC_BYTEWIDTH == 4)
    #if (LASSO_HOST_UNALIGNED_MEMORY_ACCESS == 1)
        *(uint32_t*)buffer = crc;
    #else
        uint8_t* crcp = (uint8_t*)&crc;
        numBytes = 4;
        while (numBytes--) {
            *buffer++ = *crcp++;
        }
    #endif
#else
    #error "Lasso host: invalid CRC Byte width"
#endif
}
#endif


/*!
 *  \brief Fetches data from underlying memory cells of active data cell set.
 *
 *         For registered and active data cells, this function fetches the
 *         underlying memory cell values and places them in the strobe buffer.
 *         Depending on the Byte width of memory cells, they are copied either
 *         in 1-Byte, 2-Byte or 4-Byte operations. However, on systems where
 *         unaligned 2-Byte or 4-Byte accesses are invalid, only 1-Byte access
 *         is used in copy operations.
 *         If message pack encoding is selected for host responses, the strobe
 *         packet is signalled by a invalid message pack Byte in the first Byte
 *         location of the buffer.
 *         If CRC calculation is selected, the user-supplied function computes
 *         CRC over all Bytes placed in the buffer. The CRC is appended last.
 *
 *  \return Void
*/
static void lasso_hostSampleDataCells (void) {
#if (LASSO_HOST_STROBE_EXTERNAL_SOURCE == 0)
    uint8_t* dataSpaceBufferPtr = strobe.buffer;
    dataCell* dC = dataCellFirst;
    uint16_t type;
    uint16_t count;
    const void* ptr;
#if (LASSO_HOST_UNALIGNED_MEMORY_ACCESS == 0)
    union {
        uint32_t m32;
        uint16_t m16[2];
        uint8_t m8[4];
    } atom __attribute__((aligned(4)));
#endif
#if (LASSO_HOST_STROBE_DYNAMICS == LASSO_STROBE_DYNAMIC)
    uint8_t* dataCellMaskPtr;
    uint8_t dataCellMaskBit;
#endif

#if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_COBS)
    *dataSpaceBufferPtr = 0xFF; // indicate that buffer has not been COBS en-
                                // coded yet, COBS itself places a 0x00 here
    dataSpaceBufferPtr += 2;    // access space behind COBS header
#endif

#if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_ESCS)
    *dataSpaceBufferPtr = 0x00; // indicate that buffer has not been ESCS en-
                                // coded yet, ESCS itself places a 0x7E here
    dataSpaceBufferPtr += strobe.Bytes_max, // access 2nd half of buffer
#endif

#if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_COBS) || \
    (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_ESCS)
    // write "invalid" MessagePack code to strobe buffer, such that
    // client understands that data is strobe and not response data
    *dataSpaceBufferPtr++ = LASSO_HOST_INVALID_MSGPACK_CODE;
#endif

#if (LASSO_HOST_STROBE_DYNAMICS == LASSO_STROBE_DYNAMIC)
    // initialize mask pointer and index
    dataCellMaskPtr = dataSpaceBufferPtr;
    dataCellMaskBit = 1;

    // clear mask Bytes
    for (dataCellMaskBit = 0; dataCellMaskBit < dataCellMaskBytes; dataCellMaskBit++) {
        *dataSpaceBufferPtr++ = 0;
    }
#endif

    while (dC) {
        type = dC->type;
        if (type & LASSO_DATACELL_ENABLE) {
#if (LASSO_HOST_STROBE_DYNAMICS == LASSO_STROBE_DYNAMIC)
            if ((--dC->update_rate & 0xFFFF) == 0) {
                *dataCellMaskPtr |= dataCellMaskBit;        // set mask bit
                dC->update_rate += (dC->update_rate >> 16); // reload
#else
            {
#endif
                count = dC->count;
                ptr = dC->ptr;

                switch (type & LASSO_DATACELL_BYTEWIDTH_MASK) {
                    case LASSO_DATACELL_BYTEWIDTH_1 : {
                        while(count--) {
                            *dataSpaceBufferPtr++ = *(uint8_t*)ptr;
                            ptr = (uint8_t*)ptr + 1;
                        }
                        break;
                    }
                    case LASSO_DATACELL_BYTEWIDTH_2 : {
#if (LASSO_HOST_UNALIGNED_MEMORY_ACCESS == 1)
                        while(count--) {
                            *(uint16_t*)dataSpaceBufferPtr++ = *(uint16_t*)ptr;
                            ptr = (uint16_t*)ptr + 1;
                        }
#else
                        // 1) perform atomic reads (possible since Word-aligned)
                        // 2) perform Byte-aligned writes
                        // Note: host endianness is maintained
                        while(count--) {
                            atom.m16[0] = *(uint16_t*)ptr;
                            ptr = (uint16_t*)ptr + 1;
                            *dataSpaceBufferPtr++ = atom.m8[0];
                            *dataSpaceBufferPtr++ = atom.m8[1];
                        }
#endif
                        break;
                    }
                    case LASSO_DATACELL_BYTEWIDTH_4 : {
#if (LASSO_HOST_UNALIGNED_MEMORY_ACCESS == 1)
                        while(count--) {
                            *(uint32_t*)dataSpaceBufferPtr++ = *(uint32_t*)ptr;
                            ptr = (uint32_t*)ptr + 1;
                        }
#else
                        // 1) perform atomic reads (possible since LongWord-aligned)
                        // 2) perform Byte-aligned writes
                        // Note: host endianness is maintained
                        while(count--) {
                            atom.m32 = *(uint32_t*)ptr;
                            ptr = (uint32_t*)ptr + 1;
                            *dataSpaceBufferPtr++ = atom.m8[0];
                            *dataSpaceBufferPtr++ = atom.m8[1];
                            *dataSpaceBufferPtr++ = atom.m8[2];
                            *dataSpaceBufferPtr++ = atom.m8[3];
                        }
#endif
                        break;
                    }
                    default : {}
                }
            }
        }
        dC = dC->next;
#if (LASSO_HOST_STROBE_DYNAMICS == LASSO_STROBE_DYNAMIC)
        if (dataCellMaskBit == 0x80) {
            dataCellMaskPtr++;
            dataCellMaskBit = 1;
        }
        else {
            dataCellMaskBit <<= 1;
        }
#endif
    }
#if (LASSO_HOST_STROBE_DYNAMICS == LASSO_STROBE_DYNAMIC)
    #if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_COBS)
        strobe.Bytes_total = dataSpaceBufferPtr - strobe.buffer - 2;
    #elif (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_ESCS)
        strobe.Bytes_total = dataSpaceBufferPtr - strobe.Bytes_max;
    #endif
#endif

#endif

#if (LASSO_HOST_STROBE_CRC_ENABLE == 1)
#if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_ESCS)
    lasso_hostAppendCRC(strobe.buffer + strobe.Bytes_max + 1, strobe.Bytes_total - LASSO_HOST_CRC_BYTEWIDTH - 1);
#elif (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_COBS)
    lasso_hostAppendCRC(strobe.buffer + 3, strobe.Bytes_total - LASSO_HOST_CRC_BYTEWIDTH - 1);
#else
    lasso_hostAppendCRC(strobe.buffer, strobe.Bytes_total - LASSO_HOST_CRC_BYTEWIDTH);
#endif
#endif

/* in RN mode: strobe frames must not be terminated! manual strobe capture! strobe encoding must be "NONE"!
#if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_RN)
    *dataSpaceBufferPtr++ = 0x13;   // '\r'
    *dataSpaceBufferPtr   = 0x10;   // '\n'
#endif
*/
}

/*!
 *  \brief  Get data cell based on its registration order.
 *
 *  \return Pointer to dataCell
 */
static dataCell* lasso_hostSeekDatacell (
    uint8_t num,                            //!< number in registration order
    uint32_t* bytepos                       //!< Byte position in strobe frame
) {
    dataCell* dC = dataCellFirst;
    uint16_t type;
    uint32_t pos = 0;

    while (num && dC) {
        type = dC->type;
        if (type & LASSO_DATACELL_ENABLE) {
            type &= LASSO_DATACELL_BYTEWIDTH_MASK;
            if (type) {
                pos += (uint32_t)dC->count * (uint32_t)type;
            }
            else {
                pos += dC->count;
            }
        }

        if (num) {
            dC = dC->next;
        }
        num--;
    }
    *bytepos = pos;

    return dC;
}


/*!
 *  \brief  Copy data cell name/type/count/unit/update rate/bytepos to buffer.
 *
 *  \return Void
 */
#if (LASSO_HOST_PROCESSING_MODE != LASSO_MSGPACK_MODE)
static void lasso_copyDatacellParams (
    dataCell* dC,                       //!< pointer to data cell
    uint8_t** buffer,                   //!< pointer to output buffer pointer
    uint32_t bytepos                    //!< Byte position in strobe frame
) {
    char* dest = (char*)*buffer;
    uint32_t len;

#if (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
    len = sprintf(dest, "%s,", dC->name);
    dest += len;

    len = sprintf(dest, "%u,", (unsigned short)dC->type);
    dest += len;

    len = sprintf(dest, "%u,", (unsigned short)dC->count);
    dest += len;

    len = sprintf(dest, "%s,", dC->unit);
    dest += len;

    len = sprintf(dest, "%lu,", (unsigned long)(dC->update_rate >> 16));
    dest += len;

    len = sprintf(dest, "%lu,", (unsigned long)bytepos);
    dest += len;;
#else
    // future option?, to verify
    uint8_t* src;

    len = strlen(dC->name) + 1;
    src = (uint8_t*)dC->name;
    while (len--) {
        *dest++ = *src++;
    }

    len = sizeof(dC->type);
    src = (uint8_t*)&dC->type;
    while (len--) {
        *dest++ = *src++;
    }

    len = sizeof(dC->count);
    src = (uint8_t*)&dC->count;
    while (len--) {
        *dest++ = *src++;
    }

    len = strlen(dC->unit) + 1;
    src = (uint8_t*)dC->unit;
    while (len--) {
        *dest++ = *src++;
    }

    len = sizeof(dC->update_rate);
    src = (uint8_t*)&dC->update_rate;
    while (len--) {
        *dest++ = *src++;
    }

    len = 4;
    src = (uint8_t*)&bytepos;
    while (len--) {
        *dest++ = *src++;
    }
#endif

    *buffer = (uint8_t*)dest;
}


/*!
 *  \brief  Copy value of data cell's underlying memory cell to buffer.
 *
 *          Note: endian-ness of host processor is used.
 *
 *  \return Error code.
 */
static int32_t lasso_copyDatacellValue (
    dataCell* dC,                       //!< pointer to data cell
    uint8_t** buffer                    //!< pointer to output buffer pointer
) {
#if (LASSO_HOST_PROCESSING_MODE != LASSO_ASCII_MODE)
    uint8_t* src = (uint8_t*)dC->ptr;
#endif
    char* dest = (char*)*buffer;
    uint32_t len;

#if (LASSO_HOST_STROBE_EXTERNAL_SOURCE != 0)
    if (dC->ptr == NULL) {
        len = sprintf(dest, "%hhu,", 0);
    }
    else {
#endif

    switch (dC->type & LASSO_DATACELL_TYPE_BYTEWIDTH_MASK) {
        case LASSO_BOOL:
        case LASSO_UINT8: {
#if (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            len = sprintf(dest, "%hhu,", *(unsigned char*)dC->ptr);
#else
            len = 1;
#endif
            break;
        }
        case LASSO_INT8: {
#if (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            len = sprintf(dest, "%hhi,", *(signed char*)dC->ptr);
#else
            len = 1;
#endif
            break;
        }
        case LASSO_CHAR: {
#if (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            if (dC->count == 1) {
                len = sprintf(dest, "%c,", *(char*)dC->ptr);
            }
            else {
                len = sprintf(dest, "%s,", (char*)dC->ptr);
            }
#else
            len = strlen((const char*)(dC->ptr));
#endif
            break;
        }
        case LASSO_UINT16: {
#if (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            len = sprintf(dest, "%hu,", *(unsigned short*)dC->ptr);
#else
            len = 1;
#endif
            break;
        }
        case LASSO_INT16: {
#if (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            len = sprintf(dest, "%hi,", *(signed short*)dC->ptr);
#else
            len = 2;
#endif
            break;
        }
        case LASSO_UINT32: {
#if (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            len = sprintf(dest, "%lu,", *(unsigned long*)dC->ptr);
#else
            len = 4;
#endif
            break;
        }
        case LASSO_INT32: {
#if (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            len = sprintf(dest, "%li,", *(signed long*)dC->ptr);
#else
            len = 4;
#endif
            break;
        }
        case LASSO_FLOAT: {
#if (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            len = sprintf(dest, "%f,", *(float*)dC->ptr);
#else
            len = 4;
#endif
            break;
        }
        case LASSO_DOUBLE: {
#if (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            len = sprintf(dest, "%lf,", *(double*)dC->ptr);
#else
            len = 8;
#endif
            break;
        }
        default: { return ENOTSUP; }
    }

#if (LASSO_HOST_STROBE_EXTERNAL_SOURCE != 0)
    }
#endif

#if (LASSO_HOST_PROCESSING_MODE != LASSO_ASCII_MODE)
    while (len--) {
        *dest++ = *src++;
    }
#else
    dest += len; // "len" obtained with sprintf includes only data bytes, not NULL terminator !
#endif

    *buffer = (uint8_t*)dest;

    return 0;
}


/*!
 *  \brief  Get data cell number from string.
 *
 *  \return Error code.
 */
static int32_t lasso_hostGetDatacellNumber (
    uint8_t** rb,                           //!< source string pointer (modified by this function)
    uint8_t* c                              //!< data cell number (output)
) {
    const char* cp = (const char*)(*rb);
    uint32_t ui;
#if (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
    if (sscanf(cp, "%lu", (unsigned long*)&ui) != 1) {
        return EINVAL;
    }

    // advance receiverBuffer (if more data follows after comma)
    *rb = (uint8_t*)strchr(cp, ',') + 1;

    // return value read with sscanf
    *c = (uint8_t)ui;
#else
    // future option, to verify
    *c = *cp;  // only 0...255 accepted
    (*rb)++;
#endif

    return 0;
}


/*!
 *  \brief  Get strobe period from string.
 *
 *  \return Error code.
 */
static int32_t lasso_hostGetStrobePeriod (
    uint8_t** rb,                           //!< source string pointer (modified by this function)
    uint16_t* c                             //!< strobe period (ms) (output)
) {
    const char* cp = (const char*)(*rb);
    uint32_t ui;
#if (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
    if (sscanf(cp, "%lu", (unsigned long*)&ui) != 1) {
        return EINVAL;
    }

    // advance receiverBuffer (if more data follows after comma)
    *rb = (uint8_t*)strchr(cp, ',') + 1;
#else
    // future option, to verify
    ui = ((uint16_t)cp[0] << 8) | cp[1]; // 0...2^16-1 accepted, big-endian
    *rb += 2;
#endif

    // return value read with sscanf
    *c = (uint16_t)ui;

    return 0;
}
#endif


/*!
 *  \brief  Set memory cell value read from string.
 *
 *  \return Error code.
 */
static int32_t lasso_hostSetDatacellValue (
    void* rb,                               //!< source string/msgpack pointer
    dataCell* dC                            //!< destination data cell pointer
) {
#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
     struct S_PackReader* frame_reader = (struct S_PackReader*)rb;
#else
    const char* cp = (const char*)rb;
#endif

    switch (dC->type & LASSO_DATACELL_TYPE_BYTEWIDTH_MASK) {
        case LASSO_BOOL :
        case LASSO_UINT8 : {
            // read integer 0 or 1 (for bool) or 0 ... 255 (for uint8)
            uint8_t u;

#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
            if (PackReaderGetUnsignedChar(frame_reader, &u)) {
#elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            if (sscanf(cp, "%hhu", (unsigned char*)&u) != 1) {
#else
    // todo
#endif
                return EINVAL;
            }

            if (dC->onChange) {
                if (!dC->onChange((void* const)&u)) break;   // do not store in datacell if callback refuses input
            }

            *(uint8_t*)dC->ptr = u;
            break;
        }

        case LASSO_CHAR : {
#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
            uint32_t u;

            // careful if onChange callback is defined,
            // user must master msgpack in order to interprete incoming string
            const char* cp = (const char*)frame_reader->cursor;
#endif

            if (dC->onChange) {
                if (!dC->onChange((void* const)cp)) break;   // do not store in datacell if callback refuses input
            }

#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
            if (PackReaderGetRawBytes(frame_reader, &u, (uint8_t*)dC->ptr, dC->count)) {
                return EINVAL;
            }
            memset((void*)((char*)dC->ptr + u), 0, dC->count - u);
#elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            // single char expected
            if (dC->count == 1) {
                *(char*)dC->ptr = *cp;
            }
            // string expected
            else {
                // check incoming string length ("\0" terminator not included)
                uint32_t u = (uint32_t)strlen(cp);

                // limit string length if too long for target datacell
                if (u >= dC->count) {
                    u = dC->count;
                }

                // copy chars and clear remainder of string
                memcpy((void*)dC->ptr, (const void*)cp, u);
                memset((void*)((char*)dC->ptr + u), 0, dC->count - u);
            }
#else
    // todo
#endif

            break;
        }

        case LASSO_INT8 : {
            int8_t i;

#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
            if (PackReaderGetSignedChar(frame_reader, &i)) {
#elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            if (sscanf(cp, "%hhi", (signed char*)&i) != 1) {
#else
    // todo
#endif
                return EINVAL;
            }

            if (dC->onChange) {
                if (!dC->onChange((void* const)&i)) break;   // do not store in datacell if callback refuses input
            }

            *(int8_t*)dC->ptr = (int8_t)i;
            break;
        }

        case LASSO_UINT16 : {
            uint16_t u;

#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
            if (PackReaderGetUnsignedShort(frame_reader, &u)) {
#elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            if (sscanf(cp, "%hu", (unsigned short*)&u) != 1) {
#else
    // todo
#endif
                return EINVAL;
            }

            if (dC->onChange) {
                if (!dC->onChange((void* const)&u)) break;   // do not store in datacell if callback refuses input
            }

            *(uint16_t*)dC->ptr = u;
            break;
        }

        case LASSO_INT16 : {
            int16_t i;

#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
            if (PackReaderGetSignedShort(frame_reader, &i)) {
#elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            if (sscanf(cp, "%hi", &i) != 1) {
#else
    // todo
#endif
                return EINVAL;
            }

            if (dC->onChange) {
                if (!dC->onChange((void* const)&i)) break;   // do not store in datacell if callback refuses input
            }

            *(int16_t*)dC->ptr = i;
            break;
        }

        case LASSO_UINT32 : {
            uint32_t u;

#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
            if (PackReaderGetUnsignedLong(frame_reader, &u)) {
#elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            if (sscanf(cp, "%lu", (unsigned long*)&u) != 1) {
#else
    // todo
#endif
                return EINVAL;
            }

            if (dC->onChange) {
                if (!dC->onChange((void* const)&u)) break;   // do not store in datacell if callback refuses input
            }

            *(uint32_t*)dC->ptr = u;
            break;
        }

        case LASSO_INT32 : {
            // read 4-Byte int
            int32_t i;

#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
            if (PackReaderGetSignedLong(frame_reader, &i)) {
#elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            if (sscanf(cp, "%li", (signed long*)&i) != 1) {
#else
    // todo
#endif
                return EINVAL;
            }

            if (dC->onChange) {
                if (!dC->onChange((void* const)&i)) break;   // do not store in datacell if callback refuses input
            }

            *(int32_t*)dC->ptr = i;
            break;
        }

    #ifdef ___int64_t_defined
        case LASSO_UINT64: {
            // read 8-Byte uint (endianness of host cpu unimportant since ascii-to-double conversion)
            uint64_t u;

#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
            // current msgpack implementation does not support 64 bit-wide integers
            //if (PackReaderGetUnsignedInteger(frame_reader, &u))
            {
#elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            if (sscanf(cp, "%Lu", &u) != 1) {
#else
    // todo
#endif
                return EINVAL;
            }

            if (dC->onChange) {
                if (!dC->onChange((void* const)&u)) break;   // do not store in datacell if callback refuses input
            }

            *(uint64_t*)dC->ptr = u;
            break;
        }

        case LASSO_INT64: {
            // read 8-Byte int (endianness of host cpu unimportant since ascii-to-double conversion)
            int64_t i;

#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
            // current msgpack implementation does not support 64 bit-wide integers
            //if (PackReaderGetSignedInteger(frame_reader, &i))
            {
#elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            if (sscanf(cp, "%Li", &i) != 1) {
#else
    // todo
#endif
                return EINVAL;
            }

            if (dC->onChange) {
                if (!dC->onChange((void* const)&i)) break;   // do not store in datacell if callback refuses input
            }

            *(int64_t*)dC->ptr = i;

            break;
        }
    #endif

        case LASSO_FLOAT :  {
            // read 4-Byte float
            float f;

#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
            // endiannes of host cpu unimportant since msgpack conversion
            if (PackReaderGetFloat(frame_reader, &f)) {
#elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            // endianness of host cpu unimportant since ascii-to-float conversion
            if (sscanf(cp, "%f", &f) != 1) {
#else
    // todo
#endif
                return EINVAL;
            }

            if (dC->onChange) {
                if (!dC->onChange((void* const)&f)) break;   // do not store in datacell if callback refuses input
            }

            *(float*)dC->ptr = f;
            break;
        }

        case LASSO_DOUBLE : {
            // read 8-Byte double (endianness of host cpu unimportant since ascii-to-double conversion)
            double d;

#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
            // not supported with current msgpack implementation
            //if (PackReaderGetFloat(frame_reader, &f))
            {
#elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
            if (sscanf(cp, "%lf", &d) != 1) {
#else
    // todo
#endif
                return EINVAL;
            }

            if (dC->onChange) {
                if (!dC->onChange((void* const)&d)) break;   // do not store in datacell if callback refuses input
            }

            *(double*)dC->ptr = d;
            break;
        }

        default: {
            // invalid data type
            return EINVAL;
        }
    }

    return 0;
}


/*!
 *  \brief  Establish current serial link strobe margin.
 *
 *  \return Strobe margin in [1/100%]
 */
static int32_t lasso_hostGetCycleMargin (void) {
    float period_ms = (float)lasso_strobe_period * (float)lasso_tick_period;
#if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_ESCS)
    float Bits_per_s = ((float)strobe.Bytes_total * 20000) / period_ms; // worst case ESCS overhead = 100%
#else
    float Bits_per_s = ((float)strobe.Bytes_total * 10000) / period_ms; // all overhead included (COBS & NONE)
#endif
    return (int32_t)((LASSO_HOST_BAUDRATE - Bits_per_s) * 10000 / LASSO_HOST_BAUDRATE);
}


/*!
 *  \brief  Read in command sent from client.
 *
 *  \return Void
 */
static void lasso_hostInterpreteCommand (void) {
    int32_t  msg_err = 0;       // error during message processing
#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
    uint32_t opcode;            // opcode ID
#else
    uint8_t  opcode;            // opcode ID
#endif
    uint8_t  cparam;            // various parameters associated with opcode
    uint16_t sparam;
    uint32_t lparam;
    bool     bparam;

    dataCell* dC = NULL;

    bool tiny_reply = true;     // start by assumimg smallest possible reply
    uint8_t* responseBuffer = response.buffer;

#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
    struct S_PackWriter frame_writer;
    struct S_PackReader frame_reader;

    uint32_t nargs;             // # of command arguments (incl. opcode)
                                // ... and later number of opcode parameters
#elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
    uint8_t* receiverBuffer = receiveBuffer;    // make local copy that can be modified
    receiverBuffer[response.valid] = 0;         // and terminate correctly
#endif

    response.Bytes_total = 0;

#if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_COBS)
    *responseBuffer = 0xFF; // indicate that buffer has not been COBS en-
                            // coded yet, COBS itself places a 0x00 here
    responseBuffer += 2;    // access space behind COBS header
#endif

#if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_ESCS)
    *responseBuffer = 0;    // this will launch the ESCS encoder
    responseBuffer += response.Bytes_max;   // access 2nd half of buffer
#endif

#if (LASSO_HOST_COMMAND_CRC_ENABLE == 1)
    // remember buffer start position for CRC generation
    uint8_t* crcBuffer = responseBuffer;
#endif

    // handle message
#if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
    // setup msgpack reader on "receiveBuffer" with max length "response.valid"
    PackReaderSetBuffer(&frame_reader, receiveBuffer, response.valid);
    PackReaderOpen(&frame_reader, E_PackTypeArray, &nargs);

    if (nargs == 2) {       // expect 1) opcode and 2) array of params
        msg_err = PackReaderGetUnsignedInteger(&frame_reader, &opcode);

        if (msg_err == 0) {
            msg_err = PackReaderOpen(&frame_reader, E_PackTypeArray, &nargs);

            if (msg_err == 0) {
#elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
    msg_err = sscanf((const char*)receiverBuffer++, "%c", &opcode);
    if (msg_err == 1) {
        msg_err = 0;

        msg_err = sprintf((char*)responseBuffer++, "%c", opcode);
        if (msg_err == 1) {
            msg_err = 0;
            {
#else
    // future option, todo
    opcode = *receiverBuffer++;  // max. 256 opcodes allowed here, but some are reserved, e.g. 0 = invalid opcode
    *responseBuffer++ = opcode;
    {
        {
            {
#endif
            // Unless COBS or ESC encoding has been chosen for command/response and strobe frames:
            // - for GET command opcodes (>= 'a'), response & strobe interleaving is impossible
            // - for SET command opcodes (>= 'A'), each command deals with the issue differently
            #if (LASSO_HOST_STROBE_ENCODING < LASSO_ENCODING_COBS)
                if ((lasso_strobing) && (opcode >= 'a')) {
                    return;
                }
            #endif

            #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
                // prepare response frame
                PackWriterSetBuffer(&frame_writer, responseBuffer, LASSO_HOST_RESPONSE_BUFFER_SIZE);
                PackWriterOpen(&frame_writer, E_PackTypeArray, 3);
                PackWriterPutUnsignedInteger(&frame_writer, opcode);    // send opcode back
            #endif

                switch (opcode) {
                    // LASSO_HOST_GET_x functions
                    case LASSO_HOST_GET_PROTOCOL_INFO : {

                    #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
                        PackWriterOpen(&frame_writer, E_PackTypeArray, 2);
                        PackWriterPutUnsignedInteger(&frame_writer, lasso_protocol_info);
                        PackWriterPutString(&frame_writer, (char*)&lasso_version);
                    #elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
                        msg_err = sprintf((char*)responseBuffer, "%lu,", (unsigned long)lasso_protocol_info);
                        if (msg_err > 0) {
                            responseBuffer += msg_err;
                            msg_err = sprintf((char*)responseBuffer, "v%s,", TOSTR(LASSO_HOST_PROTOCOL_VERSION));
                        }
                        if (msg_err > 0) {
                            responseBuffer += msg_err;
                            msg_err = 0;
                        }
                        else {
                            msg_err = ECANCELED;
                            break;
                        }
                    #else
                        // todo
                        *responseBuffer++ = (uint8_t)LASSO_HOST_PROTOCOL_VERSION;
                    #endif

                        tiny_reply = false;
                        break;
                    }

                    case LASSO_HOST_GET_TIMING_INFO : {

                    #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
                        PackWriterOpen(&frame_writer, E_PackTypeArray, 7);
                        PackWriterPutUnsignedInteger(&frame_writer, (uint32_t)lasso_tick_period);
                        PackWriterPutUnsignedInteger(&frame_writer, (uint32_t)LASSO_HOST_COMMAND_TIMEOUT_TICKS);
                        PackWriterPutUnsignedInteger(&frame_writer, (uint32_t)lasso_roundtrip_latency_ticks);
                        PackWriterPutUnsignedInteger(&frame_writer, (uint32_t)LASSO_HOST_STROBE_PERIOD_MIN_TICKS);
                        PackWriterPutUnsignedInteger(&frame_writer, (uint32_t)LASSO_HOST_STROBE_PERIOD_MAX_TICKS);
                        PackWriterPutUnsignedInteger(&frame_writer, (uint32_t)lasso_strobe_period);
                        PackWriterPutUnsignedInteger(&frame_writer, (uint32_t)lasso_hostGetCycleMargin());
                    #elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
                        msg_err = sprintf((char*)responseBuffer, "%li,", (signed long)lasso_tick_period);
                        if (msg_err > 0) {
                            responseBuffer += msg_err;

                            msg_err = sprintf((char*)responseBuffer, "%li,", (signed long)LASSO_HOST_COMMAND_TIMEOUT_TICKS);
                            if (msg_err > 0) {
                                responseBuffer += msg_err;

                                msg_err = sprintf((char*)responseBuffer, "%li,", (signed long)lasso_roundtrip_latency_ticks);
                                if (msg_err > 0) {
                                    responseBuffer += msg_err;

                                    msg_err = sprintf((char*)responseBuffer, "%li,", (signed long)LASSO_HOST_STROBE_PERIOD_MIN_TICKS);
                                    if (msg_err > 0) {
                                        responseBuffer += msg_err;

                                        msg_err = sprintf((char*)responseBuffer, "%li,", (signed long)LASSO_HOST_STROBE_PERIOD_MAX_TICKS);
                                        if (msg_err > 0) {
                                            responseBuffer += msg_err;

                                            msg_err = sprintf((char*)responseBuffer, "%li,", (signed long)lasso_strobe_period);
                                            if (msg_err > 0) {
                                                responseBuffer += msg_err;

                                                msg_err = sprintf((char*)responseBuffer, "%li,", (signed long)lasso_hostGetCycleMargin());
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        if (msg_err > 0) {
                            responseBuffer += msg_err;
                            msg_err = 0;
                        }
                        else {
                            msg_err = ECANCELED;
                            break;
                        }
                    #else
                        // todo
                        *(float*)responseBuffer++ = (float)LASSO_TICK_HOST_PERIOD_MS;
                    #endif

                        tiny_reply = false;
                        break;
                    }

                    case LASSO_HOST_GET_DATACELL_COUNT : {

                    #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
                         PackWriterOpen(&frame_writer, E_PackTypeArray, 1);
                         PackWriterPutUnsignedInteger(&frame_writer, (uint32_t)dataCellCount);
                    #elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
                        msg_err = sprintf((char*)responseBuffer, "%li,", (signed long)dataCellCount);
                        if (msg_err > 0) {
                            responseBuffer += msg_err;
                            msg_err = 0;
                        }
                        else {
                            msg_err = ECANCELED;
                            break;
                        }
                    #else
                        // todo
                        *responseBuffer++ = (uint8_t)dataCellCount;
                    #endif

                        tiny_reply = false;
                        break;
                    }

                    case LASSO_HOST_GET_DATACELL_PARAMS : {

                    #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
                        msg_err = PackReaderGetUnsignedInteger(&frame_reader, &lparam);
                        cparam = (uint8_t)lparam;
                    #else
                        msg_err = lasso_hostGetDatacellNumber(&receiverBuffer, &cparam);
                    #endif
                        if (msg_err) break;
                        dC = lasso_hostSeekDatacell(cparam, &lparam);

                        if (dC) {
                    #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
                            PackWriterOpen(&frame_writer, E_PackTypeArray, 6);
                            PackWriterPutString(&frame_writer, dC->name);
                            PackWriterPutUnsignedInteger(&frame_writer, (uint32_t)dC->type);
                            PackWriterPutUnsignedInteger(&frame_writer, (uint32_t)dC->count);
                            PackWriterPutString(&frame_writer, dC->unit);
                            PackWriterPutUnsignedInteger(&frame_writer, dC->update_rate >> 16);
                            PackWriterPutUnsignedInteger(&frame_writer, lparam);
                    #else
                            lasso_copyDatacellParams(dC, &responseBuffer, lparam);
                    #endif
                        }
                        else {
                            msg_err = EFAULT;
                            break;
                        }

                        tiny_reply = false;
                        break;
                    }

                    case LASSO_HOST_GET_DATACELL_VALUE : {

                    #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
                        msg_err = PackReaderGetUnsignedInteger(&frame_reader, &lparam);
                        cparam = (uint8_t)lparam;
                    #else
                        msg_err = lasso_hostGetDatacellNumber(&receiverBuffer, &cparam);
                    #endif
                        if (msg_err) break;
                        dC = lasso_hostSeekDatacell(cparam, &lparam);

                        if (dC) {
                    #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
                            PackWriterOpen(&frame_writer, E_PackTypeArray, 1);
                            switch (dC->type & LASSO_DATACELL_TYPE_BYTEWIDTH_MASK)
                            {
                                case LASSO_BOOL:   { PackWriterPutBoolean(&frame_writer, *(bool*)(dC->ptr)); break; }
                                case LASSO_CHAR:   {
                                    if (dC->count == 1) {
                                        PackWriterPutRawBytes(&frame_writer, (uint8_t*)(dC->ptr), 1);
                                    }
                                    else {
                                        PackWriterPutString(&frame_writer, (const char*)(dC->ptr));
                                    }
                                    break;
                                }
                                case LASSO_UINT8:  { PackWriterPutUnsignedInteger(&frame_writer, *(uint8_t*)(dC->ptr)); break; }
                                case LASSO_INT8:   { PackWriterPutSignedInteger(&frame_writer, *(int8_t*)(dC->ptr)); break; }
                                case LASSO_UINT16: { PackWriterPutUnsignedInteger(&frame_writer, *(uint16_t*)(dC->ptr)); break; }
                                case LASSO_INT16:  { PackWriterPutSignedInteger(&frame_writer, *(int16_t*)(dC->ptr)); break; }
                                case LASSO_UINT32: { PackWriterPutUnsignedInteger(&frame_writer, *(uint32_t*)(dC->ptr)); break; }
                                case LASSO_INT32:  { PackWriterPutSignedInteger(&frame_writer, *(int32_t*)(dC->ptr)); break; }
                                case LASSO_FLOAT:  { PackWriterPutFloat(&frame_writer, *(float*)(dC->ptr)); break; }
                                case LASSO_DOUBLE: { PackWriterPutFloat(&frame_writer, *(double*)(dC->ptr)); break; }
                                default: { msg_err = ENOTSUP; }
                            }
                            if (msg_err) break;
                    #else
                            if (lasso_copyDatacellValue(dC, &responseBuffer) != 0) {
                                msg_err = EINVAL;
                                break;
                            }
                    #endif
                        }
                        else {
                            msg_err = EFAULT;
                            break;
                        }

                        tiny_reply = false;
                        break;
                    }

                    //----------------------------//
                    // LASSO_HOST_SET_x functions //
                    //----------------------------//

                    case LASSO_HOST_SET_ADVERTISE : {
                        // strobing on : this command does not send any reply, effect can easily be seen on client side
                        // strobing off: same
                        lasso_advertise = true;

                        if (lasso_strobing) {
                            lasso_strobing = false;

                            if (actCallback) {
                                actCallback(false);
                            }
                        }

                        // since advertising and strobing is mutually exclusive, strobing is off now
                        return;
                    }

                    case LASSO_HOST_SET_STROBE_PERIOD : {
                        // advertising on: no reply is sent
                        // strobing on : tiny reply sent only for encodings COBS or ESCS
                        // strobing off: tiny reply sent (acknowledgement)
                        // Note: effect of this command can easily be observed on client side

                        // read in period from command's parameter list
                    #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
                        msg_err = PackReaderGetUnsignedInteger(&frame_reader, &lparam);
                        sparam = (uint16_t)lparam;
                    #else
                        msg_err = lasso_hostGetStrobePeriod(&receiverBuffer, &sparam);
                    #endif
                        if (msg_err) break;

                        // try to set new strobe period
                        if ((sparam >= LASSO_HOST_STROBE_PERIOD_MIN_TICKS) && (sparam <= LASSO_HOST_STROBE_PERIOD_MAX_TICKS)) {
                            //strobe_enable = true; // strobing not explicitly enabled here
                            if (perCallback) {
                                lasso_strobe_period = perCallback(sparam);
                            }
                            else {
                                lasso_strobe_period = sparam;
                            }
                            if (strobe.countdown > lasso_strobe_period) {
                                strobe.countdown = lasso_strobe_period;
                            }
                        }
                        else {
                            msg_err = EINVAL;
                            break;
                        }

                        if (lasso_advertise) {
                            return;
                        }

                    #if (LASSO_HOST_STROBE_ENCODING < LASSO_ENCODING_COBS)  // no response & strobe interleaving possible
                        if (lasso_strobing) {
                            return;
                        }
                    #endif

                        break;
                    }

                    case LASSO_HOST_SET_DATASPACE_STROBE : {
                        // advertising on: no reply is sent
                        // strobing on: tiny reply sent only for encodings COBS or ESCS
                        // strobing off: tiny reply sent (acknowledgement)
                        // Note: effect of this command can easily be observed on client side

                        // get flag from command's parameter list
                    #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
                        msg_err = PackReaderGetUnsignedInteger(&frame_reader, &lparam);
                    #elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
                        if (sscanf((const char*)receiverBuffer, "%lu", (unsigned long*)&lparam) != 1) {
                            msg_err = EINVAL;
                        }
                    #else
                        lparam = *(uint32_t*)receiverBuffer;
                    #endif
                        if (msg_err) break;

                        // switch strobing on/off
                        if (lparam) {
                            if (!lasso_strobing) {
                                strobe.countdown = 1;   // start strobing immediately
                            }
                            lasso_strobing = true;
                        }
                        else {
                            lasso_strobing  = false;    // stop strobing on next cycle
                        }

                        if (actCallback) {
                            actCallback(lasso_strobing);
                        }

                        // also stop advertising, if necessary
                        if (lasso_advertise) {
                            strobe.Byte_count = 0;      // cancel remaining frames
                            lasso_advertise = false;
                            return;                     // no response sent
                        }

                    #if (LASSO_HOST_STROBE_ENCODING < LASSO_ENCODING_COBS)  // no response & strobe interleaving possible
                        return;
                    #else
                        break;
                    #endif
                    }

                    case LASSO_HOST_SET_DATACELL_STROBE : {
                        // advertising on: no reply is sent
                        // strobing on : not possible -> this command requires strobing to be off since it changes the strobe length
                        // strobing off: acknowledgement is sent (tiny reply)
                        if (lasso_strobing) {
                            return;
                        }

                    #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
                        msg_err = PackReaderGetUnsignedInteger(&frame_reader, &lparam);
                        cparam = (uint8_t)lparam;
                    #else
                        msg_err = lasso_hostGetDatacellNumber(&receiverBuffer, &cparam);
                    #endif
                        if (msg_err) break;
                        dC = lasso_hostSeekDatacell(cparam, &lparam);

                        if (dC) {
                    #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
                            msg_err = PackReaderGetBoolean(&frame_reader, &bparam);
                    #elif (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
                            if (sscanf((char*)receiverBuffer, "%lu", (unsigned long*)&lparam) != 1) {
                                msg_err = EINVAL;
                            }
                            bparam = lparam != 0;
                    #else
                            bparam = *receiverBuffer;
                    #endif
                            if (msg_err) break;

                            lparam = dC->type & LASSO_DATACELL_ENABLE_MASK;
                            if (bparam) {
                                if (!lparam) {
                                    lparam = (uint32_t)(dC->type & LASSO_DATACELL_BYTEWIDTH_MASK);
                                    if (lparam == 0) {
                                        lparam = 1;
                                    }
                                    strobe.Bytes_total += (uint32_t)dC->count * lparam;
                                    dC->type |= LASSO_DATACELL_ENABLE_MASK;
                                }
                            }
                            else {
                                if (lparam) {
                                    lparam = (uint32_t)(dC->type & LASSO_DATACELL_BYTEWIDTH_MASK);
                                    if (lparam == 0) {
                                        lparam = 1;
                                    }
                                    strobe.Bytes_total -= (uint32_t)dC->count * lparam;
                                    dC->type &= LASSO_DATACELL_DISABLE_MASK;
                                }
                            }
                        }
                        else {
                            msg_err = EFAULT;
                            break;
                        }

                        if (lasso_advertise) {
                            return;
                        }

                        break;
                    }

                    case LASSO_HOST_SET_DATACELL_VALUE : {
                        // advertising on: no reply is sent
                        // strobing on : tiny reply sent only for encodings COBS and ESCS
                        // strobing off: tiny reply sent (acknowledgement)

                    #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
                        msg_err = PackReaderGetUnsignedInteger(&frame_reader, &lparam);
                        cparam = (uint8_t)lparam;
                    #else
                        msg_err = lasso_hostGetDatacellNumber(&receiverBuffer, &cparam);
                    #endif
                        if (msg_err) break;
                        dC = lasso_hostSeekDatacell(cparam, &lparam);

                        if (dC) {
                            if (dC->type & LASSO_DATACELL_WRITEABLE) {
                            #if (LASSO_HOST_STROBE_EXTERNAL_SOURCE != 0)
                                // temporarily use receiverBuffer for storage of interpreted value
                                // in fact, incoming values are not passed on into external source space ...
                                // ... they can only be used for control purposes in the onChange callback
                                dC->ptr = (void*)receiverBuffer;
                            #endif

                            #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
                                msg_err = lasso_hostSetDatacellValue((void*)&frame_reader, dC);
                            #else
                                msg_err = lasso_hostSetDatacellValue(receiverBuffer, dC);
                            #endif
                            }
                            else {
                                msg_err = EACCES;
                            }
                        }
                        else {
                            msg_err = EFAULT;
                        }

                        if (lasso_advertise) {
                            return;
                        }

                    #if (LASSO_HOST_STROBE_ENCODING < LASSO_ENCODING_COBS)  // no response & strobe interleaving possible
                        if (lasso_strobing) {
                            return;
                        }
                    #endif

                        break;
                    }

                    default : {
                        msg_err = EOPNOTSUPP;
                    }
                }
            }

        #if (LASSO_HOST_PROCESSING_MODE == LASSO_MSGPACK_MODE)
            if (tiny_reply) {
                PackWriterOpen(&frame_writer, E_PackTypeArray, 0);
                PackWriterPutSignedInteger(&frame_writer, msg_err);
            }
            else {
                PackWriterPutSignedInteger(&frame_writer, msg_err);
            }

            response.Bytes_total = PackWriterGetOffset(&frame_writer);
        #else
            if (tiny_reply) {
                // tiny reply can be the consequence of an error
                // -> return to offset right behind opcode
                responseBuffer = response.buffer;
                responseBuffer++;

            // correct responseBuffer pointer for COBS
            #if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_COBS)
                responseBuffer += 2;   // access space behind COBS header
            #endif

            // correct responseBuffer pointer for ESCS
            #if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_ESCS)
                responseBuffer += response.Bytes_max;   // access 2nd half of buffer
            #endif
            }

            // print error code (0 for no error)
            #if (LASSO_HOST_PROCESSING_MODE == LASSO_ASCII_MODE)
                msg_err = sprintf((char*)responseBuffer, "%li", (signed long)msg_err);
                responseBuffer += msg_err;
            #else
                *responseBuffer++ = (uint8_t)msg_err;
            #endif

            // compute transmission length (to be corrected further down for COBS and ESCS)
            response.Bytes_total = responseBuffer - response.buffer;
        #endif

    #if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_RN)
            *responseBuffer++ = 0x0D;   // '\r'
            *responseBuffer   = 0x0A;   // '\n'
            response.Bytes_total += 2;

            // no CRC generation allowed
    #else
        #if (LASSO_HOST_PROCESSING_MODE != LASSO_MSGPACK_MODE)
        // correct transmission length for COBS
        #if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_COBS)
            response.Bytes_total -= 2;  // COBS header does not count as payload Bytes
        #endif

        // correct transmission length for ESCS
        #if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_ESCS)
            response.Bytes_total -= response.Bytes_max;   // correct initial offset
        #endif
        #endif

        #if (LASSO_HOST_COMMAND_CRC_ENABLE == 1)
            lasso_hostAppendCRC(crcBuffer, response.Bytes_total);
            response.Bytes_total += LASSO_HOST_CRC_BYTEWIDTH;
        #endif

    #endif
        }
    }
}


/*!
 *  \brief  Send a strobe or response data frame.
 *
 *          For COBS encoding:
 *          - check if encoding still necessary for current frame
 *            (might have been done in last cycle, followed by COM busy)
 *          - restore 3rd Byte of frame buffer
 *          - backup 256th Byte of frame buffer
 *          - encode up to 253 Bytes payload
 *          - send with 3 Bytes overhead on serial link
 *          - if serial link busy, transmission is delayed to next lasso cycle
 *
 *          For other encodings:
 *          - data frame is cut into chunks of "LASSO_HOST_MAX_FRAME_SIZE" size
 *          - if serial link busy, transmission is delayed to next lasso cycle
 *
 *  \return TRUE if sending frame, FALSE if serial port busy or nothing to send
 */
static bool lasso_hostTransmitDataFrame (
    dataFrame* ptr                          //!< data frame pointer
) {
    uint8_t* frame = ptr->frame;
    uint32_t num = ptr->Byte_count;
    #if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_COBS)
        bool extended = false;
    #endif

    if (num > 0) {

    #if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_COBS)
    #if (LASSO_HOST_COMMAND_ENCODING != LASSO_HOST_STROBE_ENCODING)
        if (ptr == &response) {
    #else
        if (!lasso_advertise) {
    #endif
            //if (ptr == &strobe) {
            //    extended = false;
            //}

            if (num > 253) {
                num = 253;
                extended = true;
            }

            // condition for sending: COBS encoded and not busy
            // conditions for encoding: not COBS encoded and not busy
            // if (chunk is COBS coded)
            //

            // COBS encode if not already done (=COM busy in previous cycle)
            if (frame[0] != 0x00) {                             // 0x00 is COBS delimiter
                // restore and save operations for Byte crushed by COBS_encode()
                frame[2] = ptr->COBS_backup;                    // restore 3rd Byte
                ptr->COBS_backup = frame[255];                  // save 256th = next 3rd Byte
                COBS_encode((COBS_buf*)frame, num, extended);   // encode up to 253 payload Bytes
            }

            // for errors other than EBUSY, no attempt to retransmit is made!
            if (comCallback(frame, num + 3) != EBUSY) { // "num" does not include COBS header nor trailing COBS delimiter
                ptr->frame      += num;
                ptr->Byte_count -= num;
                return true;
            }

            return false;
        }
    #endif

    #if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_ESCS)
    #if (LASSO_HOST_COMMAND_ENCODING != LASSO_HOST_STROBE_ENCODING)
        if (ptr == &response) {
    #else
        if (!lasso_advertise) {
    #endif
            // ESCS encode if not already done (=COM busy in previous cycle)
            if (frame[0] != 0x7E) {             // 0x7E is ESCS delimiter
                ptr->Byte_count = ESCS_encode(ptr->frame + ptr->Bytes_max, frame, num);
                num = ptr->Byte_count;          // encoding changes length of frame !
            }
        }
    #endif

        // all other cases (RN encoded responses, un-encoded strobes) covered here:
        if (num > LASSO_HOST_MAX_FRAME_SIZE) {
            num = LASSO_HOST_MAX_FRAME_SIZE;
        }

        // for errors other than EBUSY, no attempt to retransmit is made!
        if (comCallback(frame, num) != EBUSY) {
            ptr->frame      += num;
            ptr->Byte_count -= num;
            return true;
        }
    }

    return false;
}


/*!
 *  \brief  Registers the host's internal timestamp.
 *
 *  \return Error code
 */
#if (LASSO_HOST_TIMESTAMP == 1)
static int32_t lasso_hostRegisterTimestamp (void) {
    return lasso_hostRegisterDataCell(LASSO_UINT32 | LASSO_DATACELL_ENABLE,
                                      1,
                                      (void*)&lasso_timestamp,
                                      "Timestamp",
                                      TOSTR(LASSO_HOST_TICK_PERIOD_MS) "ms",
#if (LASSO_HOST_STROBE_DYNAMICS != LASSO_STROBE_DYNAMIC)
                                      NULL);
#else
                                      NULL,
                                      1);
#endif
}
#endif


//----------------------//
// Public functions API //
//----------------------//

/*!
 *  \brief  Register user-supplied communication functions.
 *
 *  \return Error code
 */
int32_t lasso_hostRegisterCOM (
    lasso_comSetup cS,          //!< user-supplied function to setup serial COM
    lasso_comCallback cC,       //!< user-supplied callback on COM transmission
    lasso_actCallback aC,       //!< user-supplied callback on strobe activation
#if (LASSO_HOST_STROBE_CRC_ENABLE == 1) || (LASSO_HOST_COMMAND_CRC_ENABLE == 1)
    lasso_perCallback pC,       //!< user-supplied callback on period change
    lasso_crcCallback rC        //!< user-supplied CRC generator
#else
    lasso_perCallback pC        //!< user-supplied callback on period change
#endif
) {
    int32_t res;

    if (cS) {
        res = cS();

        if (res) {
            return res;
        }
    }
    else {
        return EINVAL;
    }

    if (cC) {
        comCallback = cC;
    }
    else {
        return EINVAL;
    }

    if (aC) {
        actCallback = aC;
    }

    if (pC) {
        perCallback = pC;
    }

#if (LASSO_HOST_TIMESTAMP == 1)
    lasso_hostRegisterTimestamp();
#endif

#if (LASSO_HOST_STROBE_CRC_ENABLE == 1) || (LASSO_HOST_COMMAND_CRC_ENABLE == 1)
    if (rC) {
        crcCallback = rC;
    }
    else {
        return EINVAL;
    }
#endif

    return 0;
}


/*!
 *  \brief  Register user-supplied CTRLS function.
 *
 *  \return Error code
 */
int32_t lasso_hostRegisterCTRLS (
    lasso_ctlCallback cC            //!< user-supplied CTRLS function
) {
    if (cC) {
        ctlCallback = cC;
    }
    else {
        return EINVAL;
    }

    return 0;
}


/*!
 *  \brief  Registers a data cell (link to memory cell).
 *
 *  \return Error code
 */
int32_t lasso_hostRegisterDataCell (
    uint16_t type,                      //!< memory cell type
    uint16_t count,                     //!< array size
    const void* ptr,                    //!< pointer to memory cell
    const char* const name,             //!< identifier string
    const char* const unit,             //!< unit string
#if (LASSO_HOST_STROBE_DYNAMICS != LASSO_STROBE_DYNAMIC)
    const lasso_chgCallback onChange    //!< user callback for change event
#else
    const lasso_chgCallback onChange,   //!< user callback for change event
    uint16_t update_rate                //!< update rate info
#endif
) {
    dataCell* dC = (dataCell*)LASSO_HOST_MALLOC(sizeof(dataCell));
    uint32_t dC_Bytes;

    if (dC == NULL) {
        return ENOMEM;
    }

    if (dataCellFirst == NULL) {
        dataCellFirst = dC;
    }

    if (dataCellLast != NULL) {
        dataCellLast->next = dC;
    }
    dataCellLast = dC;

#if (LASSO_HOST_STROBE_EXTERNAL_SOURCE != 0)
    type |= LASSO_DATACELL_PERMANENT;
#endif

    if (type & LASSO_DATACELL_PERMANENT) {
        type |= LASSO_DATACELL_ENABLE;
    }
    dC->type        = type;
    dC->count       = count;
#if (LASSO_HOST_STROBE_EXTERNAL_SOURCE == 0)
    if (ptr == NULL) {
        return EFAULT;
    }
#endif
    dC->ptr         = ptr;
    dC->name        = name;
    dC->unit        = unit;
    dC->onChange    = onChange;
#if (LASSO_HOST_STROBE_DYNAMICS == LASSO_STROBE_DYNAMIC)
    dC->update_rate = ((uint32_t)update_rate << 16) + update_rate;
#else
    dC->update_rate = (1 << 16) + 1;
#endif
    dC->next        = NULL;

    if (type & LASSO_DATACELL_BYTEWIDTH_MASK) {
        dC_Bytes = (uint32_t)count * (uint32_t)(type & LASSO_DATACELL_BYTEWIDTH_MASK);
    }
    else {
        dC_Bytes = (uint32_t)count;
    }
    strobe.Bytes_max += dC_Bytes;

    if (type & LASSO_DATACELL_ENABLE_MASK) {
        strobe.Bytes_total += dC_Bytes;
    }

    dataCellCount++;

    return 0;
}


/*!
 *  \brief  Prepare host's memory spaces for serial transmission.
 *
 *          Assumes that:
 *          - all memory cells to be sampled have been registered
 *          - strobe.Bytes_max and strobe.Bytes_total are known
 *              strobe.Bytes_max  : absolute maximum number of strobe Bytes
 *              strobe.Bytes_total: current number of strobe Bytes according
 *                                  to active datacells
 *          - response.Bytes_max is known
 *
 *          More than the regular minimum space is allocated if:
 *          - message packing is enabled for host responses
 *          - CRC is enabled for host responses and/or strobe
 *          - an encoding scheme is selected
 *
 *          E.g. for msgpack'ed responses, CRC and COBS encoding scheme,
 *          memory space is adjusted as follows:
 *
 *          Additional space for header:
 *          - 2 Bytes for COBS header: 0x00 (frame delimiter) + first COBS code
 *          - 1 Byte for the "invalid" MessagePack code 0xC1 in strobe frame
 *            (indicating to lasso client that frame is a strobe frame)
 *
 *          Additional space for footer:
 *          - 1 Byte for COBS phantom delimiter Byte
 *          - 1/2/4 Bytes for CRC (depending on user-specified CRC Byte width)
 *
 *          Finally, memory requirement is rounded to next alignment boundary.
 *
 *  \return Error code
 */
int32_t lasso_hostRegisterMEM (void) {

    // in strobe ESCS or COBS encoding, an "invalid" MessagePack code is
    // inserted before strobe packet for interleaving with responses packet
    #if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_ESCS) || \
        (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_COBS)
        strobe.Bytes_max += 1;
        strobe.Bytes_total += 1;
    #endif

    // add space for dynamic strobing information at the beginning of strobe packet
    #if (LASSO_HOST_STROBE_DYNAMICS == LASSO_STROBE_DYNAMIC)
        dataCellMaskBytes = ((dataCellCount - 1) >> 3) + 1;
        strobe.Bytes_max += dataCellMaskBytes;
        strobe.Bytes_total += dataCellMaskBytes;
    #endif

    // add space for CRC to end of strobe packet
    #if (LASSO_HOST_STROBE_CRC_ENABLE == 1)
        strobe.Bytes_max += LASSO_HOST_CRC_BYTEWIDTH;
        strobe.Bytes_total += LASSO_HOST_CRC_BYTEWIDTH;
    #endif

    // add space for CRC to end of response packet
    #if (LASSO_HOST_COMMAND_CRC_ENABLE == 1)
        response.Bytes_max += LASSO_HOST_CRC_BYTEWIDTH;
    #endif

    // ESCS encoding requires substantial (worst case) overhead
    #if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_ESCS)
        strobe.Bytes_max += 2;      // start and end delimiter
        //strobe.Bytes_max *= 2;      // worst case (see further below)

    // COBS encoding always requires constant overhead
    #elif (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_COBS)
        strobe.Bytes_max += 3;      // exactly 3 Bytes
        //strobe.Bytes_total += 3;  // Bytes_total must not include the COBS overhead

    // NONE (no encoding) supported
    #elif (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_NONE)

    //  RN strobe encoding not supported
    #else
        #error "Unsupported strobe encoding"
    #endif

    // ESCS encoding requires substantial (worst case) overhead
    #if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_ESCS)
        response.Bytes_max += 2;    // start and end delimiter
        //response.Bytes_max *= 2;    // worst case (see further below)

    // COBS encoding always requires constant overhead
    #elif (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_COBS)
        response.Bytes_max += 3;    // start and end delimiter + COBS code

    // RN encoding always requires constant overhead
    #elif (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_RN)
        response.Bytes_max += 2;    // two end delimiters

    // NONE (no encoding) not supported
    #else
        #error "unsupported response encoding"
    #endif

    // align requirement to system (user) specific boundary (e.g. %4==0)
    if (strobe.Bytes_max & (LASSO_MEMORY_ALIGN - 1)) {
        strobe.Bytes_max &= ~(LASSO_MEMORY_ALIGN - 1);
        strobe.Bytes_max += LASSO_MEMORY_ALIGN;
    }

    if (response.Bytes_max & (LASSO_MEMORY_ALIGN - 1)) {
        response.Bytes_max &= ~(LASSO_MEMORY_ALIGN - 1);
        response.Bytes_max += LASSO_MEMORY_ALIGN;
    }

    // ESCS uses a special memory allocation scheme (see further below)
    #if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_ESCS)
        strobe.Bytes_max *= 2;
    #endif
    #if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_ESCS)
        response.Bytes_max *= 2;
    #endif

    // don't allocate if strobe source is an external, user-specified buffer
#if (LASSO_HOST_STROBE_EXTERNAL_SOURCE == 0)
    strobe.buffer = (uint8_t*)LASSO_HOST_MALLOC(strobe.Bytes_max);
    if (strobe.buffer == NULL) {
        return ENOMEM;
    }
#endif

    response.buffer = (uint8_t*)LASSO_HOST_MALLOC(response.Bytes_max);
    if (response.buffer == NULL) {
        return ENOMEM;
    }

    receiveBuffer = (uint8_t*)LASSO_HOST_MALLOC(LASSO_HOST_COMMAND_BUFFER_SIZE);
    if (receiveBuffer == NULL) {
        return ENOMEM;
    }

    // ESCS uses a special memory allocation scheme:
    // 1) twice the minimum memory requirement (worst case) has been allocated
    // 2) total buffer size is buffer.Bytes_max
    // 3) the buffer is split in two halfs, each buffer.Bytes_max/2 long
    //    (-> therefore, we divide current buffer.Bytes_max by 2 here)
    // 4) data is written to upper half of buffer (offset = strobe.Bytes_max)
    // 5) data is encoded to lower half of buffer (offset = 0)
    //    (-> may crush data in upper half of buffer without consequence)
    #if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_ESCS)
        strobe.Bytes_max /= 2;
    #endif
    #if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_ESCS)
        response.Bytes_max /= 2;
    #endif

    return 0;
}


/*!
 *  \brief  Receive one char from user-supplied serial port.
 *
 *  \return Error code
 */
int32_t lasso_hostReceiveByte (
    uint8_t b                   //!< char from serial port
) {
    if (receiveBufferIndex < LASSO_HOST_COMMAND_BUFFER_SIZE) {
#if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_RN)
        if (b == '\n') {
            if (receiveBufferIndex == 0) {
                return ENODATA;
            }
            if (receiveBuffer[receiveBufferIndex - 1] == '\r') {
                /*
                // 1) terminate string by NULL or whitespace character (0x20)
                //    -> seems unnecessary for sscanf (works with '\r')
                //    -> however, in case of "LASSO_HOST_SET_DATACELL_VALUE"
                //       a terminating NULL character is useful
                //    -> moved to lasso_hostInterpreteCommand()
                // 2) CRC check
                //    -> not used for RN encoding
                receiveBuffer[receiveBufferIndex - 1] = 0;
                */
                response.valid = receiveBufferIndex;
                receiveBufferIndex = 0;
                return 0;
            }
            else {
                receiveBufferIndex = 0;
                return EILSEQ;     // '\n' not allowed without prior '\r'
            }
        }

        if (response.valid == 0) {   // only one command to be handled at a time
            receiveBuffer[receiveBufferIndex++] = b;
            receiveTimeout = LASSO_HOST_COMMAND_TIMEOUT_TICKS;
        }
        else {
            receiveBufferIndex = 0;
            return ENOSPC;
        }
#elif (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_COBS)
        if (response.valid == 0) {
            response.valid = COBS_decode_inline(b, receiveBuffer, LASSO_HOST_COMMAND_BUFFER_SIZE);
        }
        else {
            return ENOSPC;
        }
#else // ESCS
        if (response.valid == 0) {
            response.valid = ESCS_decode_inline(b, receiveBuffer, LASSO_HOST_COMMAND_BUFFER_SIZE);
        }
        else {
            return ENOSPC;
        }
#endif
    }
    else {
        receiveBufferIndex = 0;
        return EOVERFLOW;
    }

    return 0;
}


/*!
 *  \brief  Synchronize strobe to external data space.
 *
 *  \return Void
 */
void lasso_hostSetBuffer (
    uint8_t* buffer         //!< external buffer pointer
) {
    strobe.buffer = buffer;
}


/*!
 *  \brief  Synchronize strobe to external events.
 *
 *  \return Void
 */
void lasso_hostCountdown (
    uint16_t count          //!< cycle counts to subtract from countdown
) {
    if (count > strobe.countdown) {
        strobe.countdown = 0;
    }
    else {
        strobe.countdown -= count;
    }
}


/*!
 *  \brief  Adjust tick period.
 *
 *          Function to be used when user application changes tick period.
 *          Note: No error check whatsoever. Compliance ensured by user.
 *          Note: Suggested to change parameter type to uint16_t in future version.
 *
 *  \return Void
 */
void lasso_hostTickPeriod (
    float period            //!< new tick period in [ms]
) {
    lasso_tick_period = (uint16_t)period;

    lasso_advertise_period_ticks = (uint16_t)ceilf(LASSO_HOST_ADVERTISE_PERIOD_MS/(float)lasso_tick_period);

    // roundtrip latency calculus explained at beginning of file
    lasso_roundtrip_latency_ticks = (uint16_t)ceilf(((LASSO_HOST_COMMAND_BUFFER_SIZE + \
                                               LASSO_HOST_RESPONSE_BUFFER_SIZE) * 10 * 1000) \
                                              /LASSO_HOST_BAUDRATE/(float)lasso_tick_period + \
                                               LASSO_HOST_RESPONSE_LATENCY_TICKS) + 1;
}


/*!
 *  \brief  Lasso host communication handler.
 *
 *          This function handles all lasso host communication.
 *          It must be called from the user application at periodic intervals.
 *
 *  \return Void
 */
void lasso_hostHandleCOM (void)
{
    // verify memory allocation of receiveBuffer; if not allocated,
    // lasso_hostRegisterMEM() has not been called yet
    if (receiveBuffer == NULL) {
        return;
    }

    // reset command reception in case of timeout
    if (receiveTimeout > 0) {
        receiveTimeout--;

        if (receiveTimeout == 0) {
            receiveBufferIndex = 0;
        }
    }

    // broadcast (advertise) signature as long as not connected to lasso client
    if (lasso_advertise) {
        strobe.countdown--;
        if (strobe.countdown == 0) {
            strobe.countdown = lasso_advertise_period_ticks;

            strobe.frame = (uint8_t*)&lasso_signature;      // load buffer start
            strobe.Byte_count = sizeof(lasso_signature);    // load Byte count
        }
    }
    else

    if (lasso_strobing) {
    #if (LASSO_HOST_STROBE_EXTERNAL_SYNC == 0)
        strobe.countdown--;
    #endif
        if (strobe.countdown == 0) {
            strobe.countdown = lasso_strobe_period;

            if (strobe.Byte_count > 0) {
                // still tranmitting? -> signal overdrive
                lasso_overdrive = 1;

                strobe.valid = false;
            }
            else {
                lasso_hostSampleDataCells();

                strobe.frame = strobe.buffer;           // load buffer start
                strobe.Byte_count = strobe.Bytes_total; // load Byte count

            #if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_COBS)
                strobe.COBS_backup = strobe.buffer[2];  // save Byte crushed by
                                                        // future COBS encoding
            #endif
            }
        }
    }

    response.countdown--;
    if (response.countdown == 0) {
        response.countdown = (uint16_t)(LASSO_HOST_RESPONSE_LATENCY_TICKS);

        if (response.Byte_count == 0) {
            if (response.valid > 0) {
                #if (LASSO_HOST_COMMAND_CRC_ENABLE == 1)
                if (crcCallback(receiveBuffer, response.valid) == 0) {
                #else
                {
                #endif
                    if (receiveBuffer[0] == LASSO_HOST_SET_CONTROLS) {
                        if (ctlCallback) {
                            ctlCallback(&receiveBuffer[1]);
                        }
                    }
                    else {
                        lasso_hostInterpreteCommand();

                        response.frame = response.buffer;   // load buffer start
                        response.Byte_count = response.Bytes_total;

                    #if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_COBS)
                        response.COBS_backup = response.buffer[2];
                    #endif
                    }
                }
                #if (LASSO_HOST_COMMAND_CRC_ENABLE == 1)
                else {
                    while(1);       // todo: error handler
                }
                #endif

                response.valid = 0;
            }
        }
    }

    // 1) responses frames are sent only if no strobe is being sent
    // 2) the first free slot after a strobe is assigned to a response frame
    if (strobe.Byte_count == 0) {
        lasso_hostTransmitDataFrame(&response);
    }
    else {
        lasso_hostTransmitDataFrame(&strobe);
    }

#if (LASSO_HOST_TIMESTAMP == 1)
    lasso_timestamp++;
#endif
}
#endif