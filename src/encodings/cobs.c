/******************************************************************************/
/*                                                                            */
/*  \file       cobs.c                                                        */
/*  \date       Oct 2017                                                      */
/*  \author     Severin Leven                                                 */
/*                                                                            */
/*  \brief      Consistent overhead Byte stuffing (COBS) library              */
/*                                                                            */
/*              Encode/decode a serial bytestream reliably and efficiently.   */
/*                                                                            */
/*  This file is part of the Lasso host library. Lasso is a configurable and  */
/*  efficient mechanism for data transfer between a host (server) and client. */
/*                                                                            */
/*  All private and public API definitions, typedefs, variables, structs and  */
/*  functions related to the COBS encoding algorithm are collected here.      */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  Target CPU: any 32-bit                                                    */
/*  Ressources: CPU                                                           */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  COBS                                                                      */
/*  [0] -> encoded as [1]                                                     */
/*  [x] [0] -> encoded as [2] [x]                                             */
/*  [x] [y] [0] -> encoded as [3] [x] [y]                                     */
/*  ...                                                                       */
/*  253 * [x] [0] -> encoded as [254] 253 * [x]                               */
/*                                                                            */
/*  The COBS encoding algorithm adds 2 extra Bytes before message. The Byte   */
/*  right before message becomes the first COBS code. The very first Byte is  */
/*  the COBS delimiter 0x00. For example:                                     */
/*          [1] [0] [2] [3] [4] [0] [0] [5] [6] [7] [8]                       */
/*  -> encoded as:                                                            */
/*  [0] [2] [1] [4] [2] [3] [4] [1] [5] [5] [6] [7] [8] [0]                   */
/*                                                                            */
/*  3 Bytes overhead: COBS delimiter and first COBS code before message       */
/*                    COBS delimiter behind message (0xFF for extended msg!)  */
/*                                                                            */
/******************************************************************************/


//----------//
// Includes //
//----------//

#include "encodings/cobs.h"


//-----------------//
// Private defines //
//-----------------//

#define COBS_DEL 0x00       //!< COBS frame start and end delimiter code
#define COBS_EXT 0xFF       //!< COBS frame delimiter code for extended messages


//-------------------//
// Private variables //
//-------------------//

// used for inline COBS decoding only
static volatile struct {
    unsigned char code;         //!< latest COBS code (decrementer)
    unsigned char count;        //!< Bytes read in current frame
} COBS_ctrl = {255, 255};


//----------------------//
// Public functions API //
//----------------------//

/*!
 *  \brief  Decode a COBS-encoded message as Bytes are read in.
 *
 *          Reads in a maximum of 253 payload Bytes (single frame).
 *          No support for extended (multiple successive) frames yet.
 *
 *          A COBS frame (256 Bytes max.) consist of
 *          - start delimiter (0x00)
 *          - first COBS code (any value of 0x01 through 0xFE=254)
 *          - 253 payload characters (any value except 0x00)
 *          - end delimiter (0x00)
 *
 *          Uses the internal state struct COBS_ctrl to keep track of
 *          current COBS code and number of Bytes already decoded.
 *
 *          Payloads longer than destination buffer size are trashed.
 *
 *  \return sizeof(message) once fully received (<=253), 
 *          sizeof(destination buffer) + 1 if buffer overrun,
 *          0 otherwise
 */
uint8_t COBS_decode_inline (
    uint8_t c,      //!< received Byte
    uint8_t* dest,  //!< destination buffer
    uint8_t size    //!< size of destination buffer (1...253)
) {
    if (c == COBS_DEL) {            // check for delimiter
        c = COBS_ctrl.code;
        COBS_ctrl.code = 255;       // expect first COBS code next

        if (c == 0) {               // finished valid COBS message
            c = COBS_ctrl.count;
            COBS_ctrl.count = 0;    // reset count
            return c;
        }
        else {                      // message invalid, skip
            COBS_ctrl.count = 0;
            return 0;
        }
    }

    if (COBS_ctrl.code == 255) {    // first COBS code
        if (COBS_ctrl.count) {      // no COBS_DEL received previously
            return 0;
        }
        COBS_ctrl.code  = c;

        if (c > 1) {
            COBS_ctrl.code--;
            return 0;
        }

        c = 0;
    }
    else
    if (COBS_ctrl.code == 0) {      // subsequent COBS code
        COBS_ctrl.code = c;
        c = 0;
    }

    COBS_ctrl.code--;

    if (COBS_ctrl.count < size) {   // as long as destination fits
        dest[COBS_ctrl.count++] = c;
    }
    else {
        COBS_ctrl.code = 255;       // otherwise trash message
        return size + 1;            // return invalid size
    }

    return 0;
}


/*!
 *   COBS encoding of payload data provided in a COBS buffer.
 *
 *   A COBS buffer must have a 2 Byte header for
 *      - start delimiter (0x00)
 *      - first COBS code (any value from 0x01 to 0xFE=254)
 *
 *   Payload data to be encoded follows after header in COBS buffer.
 *   Length of payload data for COBS_encode is 253 Bytes. Longer
 *   (=extended) messages must be sliced in chunks of 253 Bytes max.
 *   Note that parameter "size" is not validated by COBS_encode() !
 *
 *   The COBS buffer must provide additional space behind the data for
 *      - end delimiter (0x00)
 *      - note: the end delimiter is 0xFF in case of extended message
 *      - the end delimiter is added behind the payload
 *
 *   \return    None.
 */
void COBS_encode (
    COBS_buf* src,      //!< source COBS buffer
    uint8_t size,       //!< number of payload Bytes (1 ... 253)
    bool extended       //!< frame part of extended message?
) {
    uint8_t c;
    uint8_t* srca = (uint8_t*)src;
    uint8_t* srcb = (uint8_t*)(&src->body); // equals "srca + 2"

    srca[0] = COBS_DEL;         // write initial COBS delimiter
    srca++;
    srcb[size] = COBS_DEL;      // write phantom delimiter after message
    size++;                     // expect max 253 payload Bytes + COBS_DEL

    while (size) {
        c = 1;                  // search next COBS_DEL
        while (*srcb++ != COBS_DEL) {
            c++;
        }

        *srca = c;
        srca += c;
        size -= c;
    }

    if (extended) {
        *srca = COBS_EXT;
    }
}