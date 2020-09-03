/******************************************************************************/
/*                                                                            */
/*  \file       escs.c                                                        */
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
/*  All private and public API definitions, typedefs, variables, structs and  */
/*  functions related to the ESCS encoding algorithm are collected here.      */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  Target CPU: any 32-bit                                                    */
/*  Ressources: CPU                                                           */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  ESCS                                                                      */
/*  0x7D is the unique character used to flag an escape sequence              */
/*  0x7E is the unique character used for message delimiting                  */
/*                                                                            */
/*  0x7D -> encoded as 0x7D 0x5D                                              */
/*  0x7E -> encoded as 0x7D 0x5E                                              */
/*                                                                            */
/*  The memory overhead of the ESCS algorithm is a priori unknown and depends */
/*  on the content of the message. Worst case situation is an overhead of the */
/*  same size as the original message.                                        */
/*                                                                            */
/******************************************************************************/


//----------//
// Includes //
//----------//

#include "encodings/escs.h"


//-----------------//
// Private defines //
//-----------------//

#define ESCS_ESC 0x7D       //!< ECSS escape sequence character
#define ESCS_DEL 0x7E       //!< ESCS frame start and end delimiter code


//-------------------//
// Private variables //
//-------------------//

// used for inline ESCS decoding only
static volatile struct {
    unsigned char state;        //!< ESCS decoding state
    unsigned char count;        //!< Bytes read in current frame
} ESCS_ctrl = {0, 0};


//----------------------//
// Public functions API //
//----------------------//

/*!
 *  \brief  Decode a ESCS-encoded message as Bytes are read in.
 *
 *          Reads in a maximum of 254 payload Bytes (single frame).
 *          No support for extended (multiple successive) frames.
 *
 *          Uses the internal state struct ESCS_ctrl to keep track of
 *          current ESCS state and number of Bytes already decoded.
 *
 *          Payloads longer than destination buffer size are trashed.
 *
 *  \return sizeof(message) once fully received (<=254), 
 *          sizeof(destination buffer) + 1 if buffer overrun,
 *          0 otherwise
 */
uint8_t ESCS_decode_inline (
    uint8_t c,                //!< received Byte
    uint8_t* dest,            //!< destination buffer
    uint8_t size              //!< size of destination buffer (1...254)
) {
    if (c == ESCS_DEL) {            // check for delimiter
        ESCS_ctrl.state = 255;      // expect message payload now

        if (ESCS_ctrl.count) {      // finished valid ESCS message
            c = ESCS_ctrl.count;
            ESCS_ctrl.count = 0;    // reset count
            return c;
        }
        else {                      // zero length message, skip
            return 0;
        }
    }

    if (c == ESCS_ESC) {            // check for escape char
        ESCS_ctrl.state = ESCS_ESC;
        return 0;
    }

    if (ESCS_ctrl.state) {
        if (ESCS_ctrl.state == ESCS_ESC) {
            ESCS_ctrl.state = 255;
            c += 0x20;
        }

        if (ESCS_ctrl.count < size) {   // as long as destination fits
            dest[ESCS_ctrl.count++] = c;
        }
        else {
            ESCS_ctrl.state = 0;        // otherwise trash message
            return size + 1;            // return invalid size
        }
    }

    return 0;
}


/*!
 *   ESCS encoding of payload data provided in a source buffer.
 *
 *   Encoded data is written to specified destination buffer.
 *
 *   The encoding process requires at least "size" Byte read &
 *   write operations as well as "2x size" compare operations.
 *   Worst case memory requirement for the destination buffer
 *   is twice the size of the source buffer plus 2 Bytes for
 *   the start and end delimiters.
 *
 *   \return    Number of Bytes in destination buffer
 */
uint32_t ESCS_encode (
    uint8_t* src,       //!< source buffer
    uint8_t* dest,      //!< destination buffer
    uint32_t size       //!< number of payload Bytes
) {
    uint8_t* dest_start = dest;
    uint8_t c;

    *dest++ = ESCS_DEL;

    while (size--) {
        c = *src++;

        if ((c == ESCS_DEL) || (c == ESCS_ESC)) {
            *dest++ = ESCS_ESC;
            c -= 0x20;
        }

        *dest++ = c;
    }

    *dest++ = ESCS_DEL;

    return (uint32_t)(dest - dest_start);
}