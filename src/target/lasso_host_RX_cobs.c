/******************************************************************************/
/*                                                                            */
/*  \file       cobs.c                                                        */
/*  \date       Oct 2017                                                      */
/*  \author     Severin Leven                                                 */
/*                                                                            */
/*  \brief      Consistent overhead Byte stuffing (COBS) library              */
/*                                                                            */
/*              Encode and decode serial data reliably and efficiently.       */
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

#include "cobs.h"			


//-----------------//
// Private defines //
//-----------------//

#define COBS_DEL 0x00       //!< COBS frame start and end delimiter code
#define COBS_EXT 0xFF		//!< COBS frame end delimiter code for extended messages

#define COBS_CODE0 0
#define COBS_CODE 1
#define COBS_DATA 2


//-------------------//
// Private variables //
//-------------------//

// used for inline COBS decoding only
static volatile struct {
	unsigned char type;         //!< indicates COBS code or data
	unsigned char code;         //!< last COBS code (decrementer)
	unsigned char count;        //!< Bytes read in current frame
} COBS_ctrl = {0, 255, 0};


//----------------------//
// Public functions API //
//----------------------//

/* not fully up to date ! (compare to c-code below)

#define COBS_DEL_S "0"					// macros for assembler
#define COBS_CODE_S "1"
#define COBS_DATA_S "2"

__asm(".global _COBS_decode_inline	\n" \
		"_COBS_decode_inline:		\n" \
		"PUSHM R4-R5				\n" \
		"MOV.L #_COBS_ctrl, R4 		\n" \

		// determine whether new Byte c is the COBS delimiter
		"CMP #"COBS_DEL_S", R1		\n" \
		"BNE 1f 					\n" \

		// COBS_ctrl.type = COBS_CODE
		"MOV.B #"COBS_CODE_S", [R4]	\n" \

		// COBS_ctrl.code0 = true
		"MOV.B #1, 1[R4]			\n" \

		// return value is COBS_ctrl.count, before zeroing
		"MOVU.B 3[R4], R1 			\n" \
		"MOV.B #0, 3[R4]			\n" \
		"RTSD #8, R4-R5				\n" \

		// if c is not the COBS delimiter, it is either a COBS code, or a payload Byte
		"1: 						\n" \
		"MOVU.B [R4], R5			\n" \
		"CMP #"COBS_CODE_S", R5		\n" \
		"BNE 2f 					\n" \

		// ... if COBS code (with or without trailing payload data Bytes):
		// write COBS_ctrl.type = COBS_DATA, COBS_ctrl.code = c
		"MOV.B #"COBS_DATA_S", [R4]	\n" \
		"MOV.B R1, 2[R4]			\n" \

		// COBS_ctrl.code0 = true?
		"BTST #0, 1[R4].B			\n" \
		"BZ 11f						\n" \
		// c > 1 ?
		"CMP #1, R1					\n" \
		"BEQ 11f					\n" \
		// COBS_ctrl.code = c-1
		"SUB #1, R1					\n" \
		"MOV.B R1, 2[R4]			\n" \
		// COBS_ctrl.code0 = false and exit(0)
		"MOV.B #0, 1[R4]			\n" \
		"XOR R1, R1					\n" \
		"RTSD #8, R4-R5				\n" \

		// c = 0
		"11:						\n" \
		"XOR R1, R1					\n" \
		"BRA 21f					\n" \

		// ... if payload Byte:
		"2:							\n" \
		"CMP #"COBS_DATA_S", R5		\n" \
		"BNE 4f						\n" \

		// decrement COBS_ctrl.code, if zero, set COBS_ctrl.sample = COBS_CODE
		"21:						\n" \
		"MOVU.B 2[R4], R5			\n" \
		"SUB #1, R5					\n" \
		"MOV.B R5, 2[R4]			\n" \
		"BNZ 3f						\n" \
		"MOV.B #"COBS_CODE_S", [R4]	\n" \
		"3:							\n" \

		// if COBS_ctrl.count < size, save c in dest and increment COBS_ctrl.count
		"MOVU.B 3[R4], R5			\n" \
		"CMP R5, R3					\n" \
		"BLEU 4f					\n" \
		"ADD R5, R2					\n" \
		"MOV.B R1, [R2]				\n" \
		"ADD #1, R5					\n" \
		"MOV.B R5, 3[R4]			\n" \

		// end with return value zero
		"4:							\n" \
		"XOR R1, R1					\n" \
		"RTSD #8, R4-R5				\n" \
	); */

/*!
 *  \brief  Decode a COBS encoded message as Bytes are read in.
 *
 *          Reads in a maximum of 253 payload Bytes (single frame).
 *          No support for extended (multiple successive) frames yet.
 *
 *          A COBS frame (256 Bytes max.) consist of
 *		    - start delimiter (0x00)
 *		    - first COBS code (any value of 0x01 through 0xFE=254)
 *		    - 253 payload characters (any value but not 0x00)
 *		    - end delimiter (0x00)
 *
 *	        Uses the internal state struct COBS_ctrl to keep track of
 *          current COBS code and number of Bytes already decoded.
 *
 *  \return Size of message once fully received, 0 otherwise
 */
/*
uint8_t COBS_decode_inline (
    uint8_t c,                //!< received Byte
    uint8_t* dest,            //!< destination buffer
    uint8_t size              //!< size of destination buffer
) {
	if (c == COBS_DEL) {
		COBS_ctrl.code = COBS_ctrl.count;   // make temporary copy
		COBS_ctrl.type = COBS_CODE0;        // expect first COBS code
		COBS_ctrl.count = 0;

		return COBS_ctrl.code;
	} 
    
	if (COBS_ctrl.type < COBS_DATA)	{
		COBS_ctrl.code = c;

		if ((COBS_ctrl.type == COBS_CODE0) && (c > 1)) {
			COBS_ctrl.code--;
		    COBS_ctrl.type = COBS_DATA;     // expect payload data now

			return 0;
		}

		COBS_ctrl.type = COBS_DATA;         // expect further payload data
		c = 0;
	}

	COBS_ctrl.code--;
	if (COBS_ctrl.code == 0) {
		COBS_ctrl.type = COBS_CODE;         // expect next COBS code
	}

	if (COBS_ctrl.count < size) { 
        dest[COBS_ctrl.count++] = c;
    }

	return 0;
}
*/

uint8_t COBS_decode_inline (
    uint8_t c,                //!< received Byte
    uint8_t* dest,            //!< destination buffer
    uint8_t size              //!< size of destination buffer
) {
	if (c == COBS_DEL) {
        c = COBS_ctrl.code;
        COBS_ctrl.code = 255;   // expect first COBS code next
        if (c == 0) {           // finished valid COBS message
    		return COBS_ctrl.count;
        }
        else {                  // message invalid, skip
            return 0;
        }
	} 
    
    // COBS_CODE0 = 255, COBS_CODE = 0, COBS_DATA = anything else
    
    if (COBS_ctrl.code == 255) {    // first COBS code
        COBS_ctrl.code  = c;
        COBS_ctrl.count = 0;
    
        if (c > 1) {
            COBS_ctrl.code--;
            return 0;
        }
        
        c = 0;
    }
    else 
    if (COBS_ctrl.code == 0) {    // subsequent COBS code
        COBS_ctrl.code = c;
        c = 0;
    }

	COBS_ctrl.code--;

	if (COBS_ctrl.count < size) { 
        dest[COBS_ctrl.count++] = c;
    }

	return 0;
}

/*!
 *   COBS encoding of payload data provided in a COBS buffer.
 *
 *   A COBS buffer must have a 2 Byte header for
 *   	- start delimiter (0x00)
 *   	- first COBS code (any value from 0x01 to 0xFE=254)
 *
 *   Payload data to be encoded follows after header in COBS buffer.
 *   Length of payload data for COBS_encode is 253 Bytes. Longer
 *   (=extended) messages must be sliced in chunks of 253 Bytes max.
 *
 *   The COBS buffer must provide additional space behind the data for
 *   	- end delimiter (0x00)
 *   	- note: the end delimiter is 0xFF in case of extended message
 *
 *   \param[src]		pointer to the source buffer
 *   \param[size]   	size of the payload data (max. 253)
 *   \param[extended]	is payload a chunk of an extended message?
 *   \return        	none
 */
void COBS_encode(COBS_buf* src, unsigned char size, bool extended) {
    uint8_t c;
    uint8_t* srca = (uint8_t*)src;
    uint8_t* srcb = (uint8_t*)(&src->body); // equals "srca + 2"
    
    srca[0] = COBS_DEL;         // write initial COBS delimiter
    srca++;
    srcb[size] = COBS_DEL;      // write phantom delimiter after message
    size++;
    
    while (size) {
        c = 1;
        while (*srcb++ != 0) {
            c++;
        }
        
        *srca = c;
        srca += c;
        
        size -= c;
    }
    
    if (extended) {
        *srca = 0xFF;
    }
}
/*
__asm(".global _COBS_encode		\n" \
		"_COBS_encode:			\n" \
		"PUSHM R3-R5			\n" \

		// write frame delimiter 0x00 in src[0]
		"XOR R5, R5 			\n" \
		"MOV.W R5, [R1+] 		\n" \

		// obtain message length in R5
		"XCHG R2, R5	 		\n" \

		// obtain pointer &src[2+message length] to behind message body in R4
		"ADD R1, R5, R4			\n" \

		// write phantom delimiter 0x00 right behind data and adjust length in R5 to include phantom Byte
		"MOV.B R2, [R4+] 		\n" \
		"ADD #1, R5				\n" \

		// store &src[1] in R4, pointer to location of cc (first COBS code)
		"MOV.L R1, R4			\n" \
		"SUB #1, R4				\n" \

		// loop start - reload R3 to 254 (max. 253 payload Bytes + phantom Byte)
		"1: 					\n" \
		"MOV.L #254, R3 		\n" \

		// search next Byte with value 0x00 (R1 = src pointer, R2 = search code 0x00, R3 = max. of 254 comparisons)
		"SUNTIL.B 				\n" \

		// obtain the number of truly compared Bytes in R3 (1-254)
		"NEG R3 				\n" \
		"ADD #254, R3 			\n" \

		// write COBS code for "array < 254 Bytes with trailing zero"
		"MOV.B R3, [R4] 		\n" \
		"ADD R3, R4				\n" \

		// adjust remaining len (R5)
		"SUB R3, R5 			\n" \

		"2: 					\n" \
		"CMP #0, R5 			\n" \
		"BNZ 1b 				\n" \

		// add trailing 0x00 (or 0xFF for extended packets) behind src data and exit
		"3: 					\n" \
		"POP R3					\n" \
		"CMP #0, R3				\n" \
		"STNZ #0xFF, R3			\n" \
		"MOV.B R3, [R4] 		\n" \
		"RTSD #8, R4-R5");
*/