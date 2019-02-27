/******************************************************************************/
/*                                                                            */
/*  \file       escs.h                                                        */
/*  \date       Nov 2017                                                      */
/*  \author     Severin Leven                                                 */
/*                                                                            */
/*  \brief      Escape sequence (ESCS) library                                */
/*                                                                            */
/*              Encode/decode a serial bytestream with an escape sequence.    */
/*                                                                            */
/*  This file is part of the Lasso host library. Lasso is a configurable and  */
/*  efficient mechanism for data transfer between a host (server) and client. */
/*                                                                            */
/*  All public API definitions, typedefs, variables, structs and functions    */
/*  related to the ESCS encoding algorithm are collected here.                */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  Target CPU: any 32-bit                                                    */
/*  Ressources: CPU                                                           */
/*                                                                            */
/******************************************************************************/

#ifndef ESCS_H
#define ESCS_H


//----------//
// Includes //
//----------//

#include <stdint.h>     // for int types
#include <stdbool.h>    // for bool type


//----------------------//
// Public functions API //
//----------------------//

#ifdef __cplusplus
extern "C" {
#endif

/*!
 *  \brief  Decode a ESCS message as Bytes are read in.
 *
 *          Reads in a maximum of 255 payload Bytes.
 *
 *  \return Size (<=254) of message once fully received, 0 otherwise
 */
uint8_t ESCS_decode_inline (
    uint8_t c,                //!< received Byte
    uint8_t* dest,            //!< destination buffer
    uint8_t size              //!< size of destination buffer
);

/*!
 *  \brief  Encode payload frame using ESCS algorithm.
 *
 *  \return Number of Bytes in destination buffer
 */
uint32_t ESCS_encode (
    uint8_t* src,           //!< source buffer
    uint8_t* dest,          //!< destination buffer
    uint32_t size           //!< number of payload Bytes
);

#ifdef __cplusplus
}
#endif

#endif /* ESCS_H */