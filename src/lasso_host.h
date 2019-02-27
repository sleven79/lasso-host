/******************************************************************************/
/*                                                                            */
/*  \file       lasso_host.h                                                  */
/*  \date       Jan 2017 - Feb 2019                                           */
/*  \author     Severin Leven                                                 */
/*                                                                            */
/*  \brief      Lasso host (data server) library API                          */
/*                                                                            */
/*  This file is part of the Lasso host library. Lasso is a configurable and  */
/*  efficient mechanism for data transfer between a host (server) and client. */
/*                                                                            */
/*  All public API definitions, typedefs and functions are exposed here.      */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  Target CPU: any 32-bit                                                    */
/*  Ressources: CPU (for com/timer ressources see target-specific module)     */
/*                                                                            */
/******************************************************************************/

#ifndef LASSO_HOST_H
#define LASSO_HOST_H


//----------//
// Includes //
//----------//

#include <stdint.h>     // for int data types
#include <stdbool.h>    // for bool data type
#include <math.h>       // for float/double data types
#include "lasso_errno.h"// errno.h replacement


//-----------------------------------//
// Definitions related to data cells //
//-----------------------------------//

// type codes:
// b0: strobe enabled
// b1: Byte-width = 2       if bits 1-3 = 0b000 -> Byte-width = 1
// b2: Byte-width = 4
// b3: Byte-width = 8
// b4-b7: type:
//        0 = bool,
//        1 = char/string,
//        2 = uint,
//        3 = int,
//        4 = float/double
//        5-8 = reserved
// b8: writeable by client
// b9: permanent strobe member
// b10-b15: reserved

#define LASSO_DATACELL_ENABLE       (0x0001)    //!< default strobe member?

#define LASSO_BOOL                  (0x0000)
#define LASSO_CHAR                  (0x0010)
#define LASSO_UINT8                 (0x0020)
#define LASSO_INT8                  (0x0030)
#define LASSO_UINT16                (0x0022)
#define LASSO_INT16                 (0x0032)
#define LASSO_UINT32                (0x0024)
#define LASSO_INT32                 (0x0034)
#ifdef ___int64_t_defined
    #define LASSO_UINT64            (0x0028)
    #define LASSO_INT64             (0x0038)
#endif
#define LASSO_FLOAT                 (0x0044)
#define LASSO_DOUBLE                (0x0048)

#define LASSO_DATACELL_WRITEABLE    (0x0100)    //!< writeable by client?
#define LASSO_DATACELL_PERMANENT    (0x0200)    //!< permanent strobe member?


//-------------------------------//
// Definitions related to strobe //
//-------------------------------//

#define LASSO_STROBE_SLOWEST        (65535) //!< maximum cycles between strobes


//----------------------------------//
// Definitions related to encodings //
//----------------------------------//

#define LASSO_ENCODING_NONE         (0)     //!< no escaping/packet delimiter
// only possible for strobe frames, not for command/response frames

#define LASSO_ENCODING_RN           (1)     //!< '\r\n' packet delimiter
// delimiter must be unique; typically used with terminal (ascii)

#define LASSO_ENCODING_COBS         (2)     //!< 0x00 COBS delimiter
// delimiter guaranteed to be unique by COBS; overhead known

#define LASSO_ENCODING_ESCS         (3)     //!< 0x7E ESCS delimiter
// delimiter guaranteed to be unique by ESCS encoding scheme; overhead unknown


//----------------------------------------//
// Definitions related to strobe dynamics //
//----------------------------------------//

#define LASSO_STROBE_STATIC         (0)     //!< static strobe size

#define LASSO_STROBE_DYNAMIC        (1)     //!< strobe adjusts to datacell periods


//-------------------------------------------//
// Definitions related to message processing //
//-------------------------------------------//

#define LASSO_ASCII_MODE            (0)     //!< message content is ASCII

#define LASSO_MSGPACK_MODE          (1)     //!< message content is msgpack'ed

// although a LASSO_RAW_MODE was planned once, this option has been discarded on Feb 25, 2019
// note that there are still lots of references to a "future" third processing mode in lasso_host.c !


//-------------------------------------//
// Include config for user application //
//-------------------------------------//
#include "lasso_host_config.h"


//-----------------//
// Public typedefs //
//-----------------//

/*!
 *  \brief Callback for setting up serial port.
 *
 *  \return  0 (no error), -1 (any other error)
 */
typedef int32_t(*lasso_comSetup)(void);

/*!
 *  \brief  Callback for serial line tranmission trigger.
 *
 *  \param[in]  source buffer start address
 *  \param[in]  number of Bytes to transmit
 *  \return     0 (no error), 16 (EBUSY, errno.h), -1 (any other error)
 */
typedef int32_t(*lasso_comCallback)(uint8_t*, uint32_t);

/*!
 *  \brief  Callback for CRC generation.
 *
 *          User-supplied CRC generation is optional
 *          CRC Byte width is specified in lasso_host_config.h
 *
 *  \param[in]  buffer pointer
 *  \param[in]  number of Bytes to iterate over
 *  \return     right-aligned 32-bit CRC value
 */
typedef uint32_t(*lasso_crcCallback)(uint8_t*, uint32_t);

/*!
 *  \brief  Callback for strobe activation/deactivation event.
 *
 *  \param[in]  TRUE for activation, FALSE for deactivation
 *  \return     Void
 */
typedef void(*lasso_actCallback)(bool);

/*!
 *  \brief  Callback for data cell change event.
 *
 *  \param[in]  pointer to new data cell value (type must be known by user)
 *  \return     Bool = change accepted (True) to not (False)
 */
typedef bool(*lasso_chgCallback)(void* const);

/*!
 *  \brief  Callback for strobe period change event.
 *
 *  \param[in]  strobe period requested [lasso cycles]
 *  \return     strobe period to be set [lasso cycles]
 */
typedef uint16_t(*lasso_perCallback)(uint16_t);

/*!
 *  \brief  Callback for user control input.
 *
 *  \param[in]  pointer to ctrls array (format must be known by user)
 *  \return     Void
 */
typedef void(*lasso_ctlCallback)(uint8_t* ctrls);


//----------------------//
// Public functions API //
//----------------------//

#ifdef __cplusplus
extern "C" {
#endif

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
);

/*!
 *  \brief  Register user-supplied CTRLS function.
 *
 *  \return Error code
 */
int32_t lasso_hostRegisterCTRLS (
    lasso_ctlCallback cC            //!< user-supplied CTRLS function
);

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
);

/*!
 *  \brief  Prepare host's memory spaces for serial transmission.
 *
 *  \return Error code
 */
int32_t lasso_hostRegisterMEM (void);

/*!
 *  \brief  Receive one char from user-supplied serial port.
 *
 *  \return Error code
 */
int32_t lasso_hostReceiveByte (
    uint8_t b                   //!< char from serial port
);

/*!
 *  \brief  Synchronize strobe to external data space.
 *
 *  \return Void
 */
void lasso_hostSetBuffer (
    uint8_t* buffer         //!< external buffer pointer
);

/*!
 *  \brief  Synchronize strobe to external events.
 *
 *  \return Void
 */
void lasso_hostCountdown (
    uint16_t count          //!< cycle counts to subtract from countdown
);

/*!
 *  \brief  Adjust tick period.
 *
 *      Note: Suggested to change parameter type to uint16_t in future version.
 *
 *  \return Void
 */
void lasso_hostTickPeriod (
    float period            //!< new tick period in [ms]
);

/*!
 *  \brief  Lasso host communication handler.
 *
 *  \return Void
 */
void lasso_hostHandleCOM (void);


#ifdef __cplusplus
}
#endif

#endif /* LASSO_HOST_H */