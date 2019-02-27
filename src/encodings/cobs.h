/******************************************************************************/
/*                                                                            */
/*  \file       cobs.h                                                        */
/*  \date       Oct 2017                                                      */
/*  \author     Severin Leven                                                 */
/*                                                                            */
/*  \brief      Consistent overhead Byte stuffing (COBS) library API          */
/*                                                                            */
/*              Encode/decode a serial bytestream reliably and efficiently.   */
/*                                                                            */
/*  This file is part of the Lasso host library. Lasso is a configurable and  */
/*  efficient mechanism for data transfer between a host (server) and client. */
/*                                                                            */
/*  All public API definitions, typedefs, variables, structs and functions    */
/*  related to the COBS encoding algorithm are collected here.                */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  Target CPU: any 32-bit                                                    */
/*  Ressources: CPU                                                           */
/*                                                                            */
/******************************************************************************/

#ifndef COBS_H
#define COBS_H


//----------//
// Includes //
//----------//

#include <stdint.h>     // for int types
#include <stdbool.h>    // for bool type


//-----------------//
// Public typedefs //
//-----------------//

typedef struct {
    uint8_t header[2];  //!< placeholder: delimiter 0x00 and first COBS code
    uint8_t body;       //!< placeholder: message body can be of any size
} COBS_buf;


//----------------------//
// Public functions API //
//----------------------//

#ifdef __cplusplus
extern "C" {
#endif

/*!
 *  \brief  Decode a COBS message as Bytes are read in.
 *
 *          Reads in a maximum of 253 payload Bytes.
 *
 *  \return size of message once fully received, 0 otherwise
 */
uint8_t COBS_decode_inline (
    uint8_t c,                //!< received Byte
    uint8_t* dest,            //!< destination buffer
    uint8_t size              //!< size of destination buffer
);

/*!
 *  \brief  Encode up to 253 payload Bytes using COBS algorithm.
 *
 *  \return Void
 */
void COBS_encode (
    COBS_buf* src,          //!< source COBS buffer
    uint8_t size,           //!< number of payload Bytes (1 ... 253)
    bool extended           //!< frame part of extended message?
);

#ifdef __cplusplus
}
#endif

#endif /* COBS_H */