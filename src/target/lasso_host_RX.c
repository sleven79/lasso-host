// -----------------
// Lasso data server
// -----------------
// Host implementation v0.1
//
// - host make data cells available to client through a data link
// - Lasso implementation is independent of data link
// - physical implementation of data link is user-provided
// 
// Lasso provides bi-directional, configurable data transfer 
// between a host and a client, with preference for data download from host to client.
// Data downloads are organized as bulk transfers = a snapshot
// of the selected data space is transfered at a specific rate (strobe).
// The transfer rate is identical for all data cells in the selected
// data space. Transfer rate is derived from the system rate by an
// integer divider. A unique time stamp is added to each bulk
// transfer. Bulk transfers can be assumed to go missing or be in-
// complete. While loosing data is not avoided by re-transmission, a
// time stamp can help detect gaps. Focus is more on timeliness and
// precision of data space transfer rate.
//
// Definitions:
// - Data cell (DC)           = a memory cell of configurable size and interpretation on the host
// - Download (DL)            = from host to client
// - Upload (UL)              = from client to host
// - System update rate (SUR) = maximum update rate of DCs in the system (not all DCs may provide maximum update rate)
// - Cell update rate (CUR)   = update rate of an individual DC
// - Download rate (DLR)      = bulk download rate (integer divider of SUR)
// - Time stamp (TS)          = unique code for each bulk transfer (incrementing at DLR)
//
// Each data cell is associated with (host-defined):
// - a type                   = char, int, uint, float and Byte-width information
// - a count                  = array length       
// - an ASCII name string     = a unique variable identifier, e.g. "my_variable"
// - an ASCII unit string     = e.g. "rad/s" or "m/s"
// - a cell update rate       = information about individual DC update rate
//
// Lasso provides different encoding schemes:
// - No encoding (packet length is known to host and client, and assuming no data is lost in transfer)
// - Simple escaping
// - COBS encoding
// - ...
//
// Lasso clients discover the data cells and transfer settings that hosts have to offer.
//
// Lasso clients must then interprete raw data sent by host.

#include "lasso_host.h"

#ifdef INCLUDE_LASSO_HOST
    
#if (LASSO_HOST_ASCII_ENABLE == 1)
    #include <stdio.h>
#endif
    
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
    
#if (LASSO_HOST_MSGPACK_ENABLE == 1)
    #include "msgpack.h"
#endif

#if (LASSO_HOST_OUTGOING_ENCODING == LASSO_ENCODING_COBS)
    #include "COBS.h"
#endif

//---------//
// Defines //
//---------//

// Lasso host parameters
#define LASSO_HOST_PROTOCOL_VERSION         (0x01)      // Lasso host protocol software version number reported on LASSO_HOST_GET_PROTOCOL_VERSION request

// Lasso host input commands
#define LASSO_HOST_INVALID_OPCODE           (0x00)

#define LASSO_HOST_SET_ADVERTISE            'A'         // return to advertising mode (default start-up mode)
#define LASSO_HOST_SET_STROBE_PERIOD        'P'         // sets the strobe period
#define LASSO_HOST_SET_DATACELL_STROBE      'S'  	    // set flag for strobe enable/disable of specific datacell
#define LASSO_HOST_SET_DATACELL_VALUE       'V' 		// set value of datacell x
#define LASSO_HOST_SET_DATASPACE_STROBE     'W'         // set flag for strobe enable/disable of entire dataspace

#define LASSO_HOST_GET_PROTOCOL_INFO        'i' 		// get protocol information (encoding protocol and version)
#define LASSO_HOST_GET_TIMING_INFO          't'         // get base lasso host tick period, command timeout, response latency, min strobe period, max strobe period, current strobe period
#define LASSO_HOST_GET_DATACELL_COUNT       'n'         // get the number of variables that are hosted
#define LASSO_HOST_GET_DATACELL_PARAMS      'p' 		// get parameters of variable x (ctrl, count, name)
#define LASSO_HOST_GET_DATACELL_VALUE       'v' 		// get value of variable x

#define LASSO_HOST_SET_CONTROLS             (0xC1)      // used to distinguish between controls and commands

// Lasso data cell type definitions
#define LASSO_DATACELL_BYTEWIDTH_1          (0x0000)
#define LASSO_DATACELL_BYTEWIDTH_2          (0x0002)
#define LASSO_DATACELL_BYTEWIDTH_4          (0x0004)
#define LASSO_DATACELL_BYTEWIDTH_5          (0x0008)    

#define LASSO_DATACELL_ENABLE_MASK          (0x0001)
#define LASSO_DATACELL_DISABLE_MASK         (0xFFFE)
#define LASSO_DATACELL_BYTEWIDTH_MASK       (0x000E)
#define LASSO_DATACELL_TYPE_MASK            (0x00F0)
#define LASSO_DATACELL_TYPE_SHIFT           (4)
#define LASSO_DATACELL_TYPE_BYTEWIDTH_MASK  (LASSO_DATACELL_TYPE_MASK | LASSO_DATACELL_BYTEWIDTH_MASK)
    
#define LASSO_MEMORY_ALIGN          (4)             // Byte boundary alignment for broadcast and response buffers    
    
#if (LASSO_HOST_ASCII_ENABLE == 0)                  // receive (incoming) buffer size for lasso host (max. 64 Bytes)
    #define LASSO_HOST_RX_BUFFER_SIZE   (16)            
#else    
    #define LASSO_HOST_RX_BUFFER_SIZE   (32)
#endif
    
#if (LASSO_HOST_ASCII_ENABLE == 0)                  // response (outgoing) buffer size for short, sporadic responses to client (may contain strings..., max. 256 Bytes)    
    #define LASSO_HOST_RESPONSE_BUFFER_SIZE (96)       
#else
    #define LASSO_HOST_RESPONSE_BUFFER_SIZE (96)       
#endif    


// HEAP requirements of lasso host:
// - LASSO_HOST_RESPONSE_BUFFER_SIZE (e.g. 196) + 
// - LASSO_HOST_RX_BUFFER_SIZE (e.g. 32) +
// - some overhead for msgpack'ing, CRC, RN, ESC, COBS coding (max. 16 Bytes) + 
// - 2 dataFrames (à 24 Bytes) +
// - N dataCells (à 24 Bytes) 
// ----> with above examples, we have 288 + N x 24 Bytes


//--------------------//
// Private Prototypes //
//--------------------//

static uint32_t lasso_crcDummyCallback(uint8_t* src, uint32_t cnt);
    
//------------------//
// Private Typedefs //
//------------------//

typedef struct DATACELL         // 28 Bytes total (packed), up to 32 Bytes total (on unpacked 32-bit system)
#if (LASSO_HOST_UNALIGNED_MEMORY_ACCESS == 1)
    __attribute__((__packed__))
#endif    
{
	uint16_t type;	            // composed of identifier (char, int, uint, float) in high nibble, Byte width and enable in low nibble
	uint16_t count;             // data cell can be an array of above atomic type
	const void* ptr;            // pointer to location of data cell content
	const char* name;           // data cell name string
    const char* unit;           // data cell unit string
    lasso_chgCallback onChange; // on change callback
    uint32_t update_rate;       // update rate of this data cell, integer divider of system update rate
	struct DATACELL* next;      // singly-linked list
} dataCell;

typedef struct DATAFRAME        // 18 Bytes total (packed), up to 32 Bytes total (on unpacked 32-bit system)
#if (LASSO_HOST_UNALIGNED_MEMORY_ACCESS == 1)
    __attribute__((__packed__))
#endif    
{      
	uint16_t countdown;         // period countdown
	uint8_t  COBS_backup;       // COBS backup Byte (only used when COBS encoding is selected)
	uint8_t  valid;             // strobe frame: valid transmission within alloted period; response frame: valid request received
	uint8_t* buffer;            // dataframe buffer pointer
    uint8_t* frame;             // dataframe frame pointer (frames are progressive parts in buffer)
	uint32_t Byte_count;        // number of Bytes remaining to be transmitted in current frame
    uint32_t Bytes_max;         // absolute maximum number of Bytes that dataframe can hold
    uint32_t Bytes_total;       // current number of Bytes in dataframe to be transmitted
} dataFrame;


//-------------------//
// Private Variables //
//-------------------//

static uint8_t   dataCellCount = 0;			                // number of registered DCs (max. 255 so far)
static dataCell* dataCellFirst = NULL;				        // pointer to first DC structure
static dataCell* dataCellLast = NULL;				        // pointer to last DC structure

static uint8_t*  receiveBuffer = NULL;                      // buffer for incoming messages (commands)
static uint8_t   receiveBufferIndex = 0;                    // write index in receiveBuffer
static uint32_t  receiveTimeout = 0;                        // receive timeout counter

static bool      lasso_strobing = false;                    // global strobe enable flag
static bool      lasso_advertise = true;                    // advertise as long as no client connection

static lasso_comCallback comCallback = NULL;                // user-supplied function for triggering communication
static lasso_crcCallback crcCallback = lasso_crcDummyCallback;  // user-supplied function for CRC generation, pre-initialized with dummy function
static lasso_offCallback offCallback = NULL;                // user-supplied function called each time host finishes handleCOM
static lasso_actCallback actCallback = NULL;                // user-supplied function called each time strobing is switched on or off
static lasso_perCallback perCallback = NULL;                // user-supplied function called each time the strobe period is changed

static dataFrame strobe    = { LASSO_HOST_STROBE_PERIOD_TICKS   , 0, true , NULL, NULL, 0, 0, 0};
static dataFrame response  = { LASSO_HOST_RESPONSE_LATENCY_TICKS, 0, false, NULL, NULL, 0, LASSO_HOST_RESPONSE_BUFFER_SIZE, 0};

static uint16_t lasso_strobe_period = LASSO_HOST_STROBE_PERIOD_TICKS;
static uint32_t lasso_overdrive = 0;

// Notes for protocol info:
// RX buffer size: 64 Bytes max
// Response buffer size: 256 Bytes max
// Frame size: 256 Bytes min, 65536 Bytes max
#define LASSO_PROTOCOL_INFO (((uint32_t)LASSO_HOST_COMMAND_ENCODING) \
                             + ((uint32_t)(LASSO_HOST_COMMAND_ENCODING == LASSO_HOST_STROBE_ENCODING) << 2) \
                             + ((uint32_t)LASSO_HOST_MSGPACK_ENABLE << 3) \
                             + ((uint32_t)LASSO_HOST_ASCII_ENABLE << 4) \
                             + (((uint32_t)LASSO_HOST_CRC_BYTEWIDTH - 1) << 5) \
                             + ((uint32_t)LASSO_HOST_COMMAND_CRC_ENABLE << 7) \
                             + ((uint32_t)LASSO_HOST_STROBE_CRC_ENABLE << 8) \
                             + ((uint32_t)LASSO_HOST_LITTLE_ENDIAN << 9) \
                             + (((uint32_t)LASSO_HOST_RX_BUFFER_SIZE - 1) << 10) \
                             + (((uint32_t)LASSO_HOST_RESPONSE_BUFFER_SIZE - 1) << 16) \
                             + ((((uint32_t)LASSO_HOST_MAX_FRAME_SIZE >> 8) - 1) << 24))

static uint32_t lasso_protocol_info = LASSO_PROTOCOL_INFO;

// 16 character signature that a lasso host broadcasts (advertises) when not connected to lasso client
#if (LASSO_HOST_SIGNATURE_IN_SRAM == 1) // place signature in SRAM
static struct __attribute__((__packed__)) {
#else    // place signature in Flash
static const struct __attribute__((__packed__)) {
#endif    
    const char head[10];
    uint32_t info;
    const char tail[2];
} signature = {"lassoHost/", LASSO_PROTOCOL_INFO, "\r\n"};  // note: trailing \0 for strings is dropped
                                    
#if (LASSO_HOST_TIMESTAMP == 1)
    static uint32_t lasso_timestamp = 0;
#endif

#if (LASSO_HOST_MSGPACK_ENABLE == 1)
    static struct S_PackWriter frame_writer;			// msgpack writer
    static struct S_PackReader frame_reader;			// msgpack reader
#endif


//-------------------//
// Private functions //
//-------------------//

// Dummy CRC caller function (in case user forgets to register his own), uses simple 8bit XORing
static uint32_t lasso_crcDummyCallback(uint8_t* src, uint32_t cnt) {  
    uint8_t CRC_value = 0;
    
    while (cnt--) {
        CRC_value ^= *src++;
    }
    
    return (uint32_t)CRC_value;
}


#if (LASSO_HOST_STROBE_CRC_ENABLE == 1)
static void lasso_hostAppendCRC(uint8_t* buffer, uint32_t numBytes) {
    uint32_t crc = crcCaller(buffer, numBytes);
    buffer += numBytes;
    
#if (LASSO_HOST_CRC_BYTEWIDTH == 1)
    *buffer = (uint8_t)crc;
#elif (LASSO_HOST_CRC_BYTEWIDTH == 2)
    #if (LASSO_HOST_UNALIGNED_MEMORY_ACCESS == 1)
        *(uint16_t*)buffer = (uint16_t)crc;
    #else
        uint8_t* crcp = (uint8_t*)&crc;
        numBytes = 2;
        while (numBytes--) {
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


/*
 * This function fetches all registered data cells and places their values one after the other in "dataSpaceBuffer".
 * Depending on the Byte-width of the registered data cell, it is copied either as char, short or long.
 * 
 * If message pack encoding is selected for host responses, the broadcast packet is signalled by a invalid message pack
 * Byte in the first Byte location of "dataSpaceBuffer".
 * 
 * If CRC calculation is selected, the user-supplied function computes the CRC over all Bytes placed in "dataSpaceBuffer" 
 * and appended at the end of the variables.
*/
 
static void lasso_hostSampleDataCells(void) {
#if (LASSO_HOST_STROBE_EXTERNAL_SOURCE == 0)     
    uint8_t* dataSpaceBufferPtr = strobe.buffer;
	dataCell* dC = dataCellFirst;
	uint16_t type;
	uint16_t count;
	const void* ptr;

#if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_COBS)    
    *dataSpaceBufferPtr = 0xFF;     // indication that buffer has not yet been COBS encoded (COBS encoding itself places a 0x00 here)
    dataSpaceBufferPtr += 2;        // space for COBS header
#endif
    
#if (LASSO_HOST_MSGPACK_ENABLE == 1)
    // write "invalid" MessagePack code to destination buffer, such that receiver understands that data shall not be decoded with MessagePack
	*dataSpaceBufferPtr++ = 0xC1;
#endif

	while (dC) {
		type = dC->type;
		if (type & LASSO_DATACELL_ENABLE) {
			count = dC->count;
			ptr = dC->ptr;

#if (LASSO_HOST_UNALIGNED_MEMORY_ACCESS == 1)            
			switch (type & LASSO_DATACELL_BYTEWIDTH_MASK) {
				case LASSO_DATACELL_BYTEWIDTH_1 : {
					while(count--) {
						*dataSpaceBufferPtr++ = *(uint8_t*)ptr)++;
					}
					break;
				}
				case LASSO_DATACELL_BYTEWIDTH_2 : {
					while(count--) {
						*(uint16_t*)dataSpaceBufferPtr++ = *(uint16_t*)ptr++;
					}
					break;
				}
				case LASSO_DATACELL_BYTEWIDTH_4 : {
					while(count--) {
						*(uint32_t*)dataSpaceBufferPtr++ = *(uint32_t*)ptr++;
					}
					break;
				}
				default : {}
			}
#else
            type &= LASSO_DATACELL_BYTEWIDTH_MASK;
            if (type) {
                count *= type;
            }
            
			while(count--) {
				*dataSpaceBufferPtr++ = *(uint8_t*)ptr++;
			}            
#endif            
		}
		dC = dC->next;
	}
#endif    
    
#if (LASSO_HOST_STROBE_CRC_ENABLE == 1)
    lasso_hostAppendCRC(strobe.buffer, strobe.Bytes_total - LASSO_HOST_CRC_BYTEWIDTH);
#endif

/* in RN mode: strobe frames must not be terminated! manual strobe capture!
#if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_RN)
    *dataSpaceBufferPtr++ = 0x13;   // '\r'
    *dataSpaceBufferPtr   = 0x10;   // '\n'
#endif  
*/
}


// returns pointer to dataCell number "num" and Byte start position in data space
static dataCell* lasso_hostSeekDatacell(uint8_t num, uint32_t* bytepos) {
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


// copies data cell type, count, name, unit, update rate
static void lasso_copyDatacellParams(dataCell* dC, uint8_t** buffer, uint32_t bytepos) {
    char* dest = (char*)*buffer;
    uint32_t len;
    
#if (LASSO_HOST_ASCII_ENABLE == 1)
    len = sprintf(dest, "%s,", dC->name);
    dest += len;    
    
    len = sprintf(dest, "%d,", dC->type);
    dest += len;
    
    len = sprintf(dest, "%d,", dC->count);
    dest += len;    
    
    len = sprintf(dest, "%s,", dC->unit);
    dest += len;
    
    len = sprintf(dest, "%lu,", dC->update_rate);
    dest += len;
    
    len = sprintf(dest, "%lu,", bytepos);
    dest += len;;
#else    
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


// copies data cell values to buffer, using same endian-ness as host processor
static int32_t lasso_copyDatacellValue(dataCell* dC, uint8_t** buffer) {
#if (LASSO_HOST_ASCII_ENABLE != 1)     
    uint8_t* src = (uint8_t*)dC->ptr;
#else
    int val;
#endif    
    char* dest = (char*)*buffer;    
    uint32_t len;
    
	switch (dC->type & LASSO_DATACELL_TYPE_BYTEWIDTH_MASK) {
		case LASSO_BOOL:
		case LASSO_UINT8: {
#if (LASSO_HOST_ASCII_ENABLE == 1)      
            val = *(unsigned char*)dC->ptr;    
            len = sprintf(dest, "%u,", val);   
#else            
            len = 1;
#endif    
            break;
        }
		case LASSO_INT8: {    
#if (LASSO_HOST_ASCII_ENABLE == 1)         
            val = *(char*)dC->ptr;
            len = sprintf(dest, "%d,", val);   
#else            
            len = 1;
#endif    
            break;
        }           
		case LASSO_CHAR: {
#if (LASSO_HOST_ASCII_ENABLE == 1)         
            if (dC->count == 1) {
                len = sprintf(dest, "%c,", *(char*)dC->ptr);
            }
            else {
                len = sprintf(dest, "%s,", (char*)dC->ptr);   // writes string including NULL terminator, but length returned includes only data Bytes
            }
#else               
            len = strlen((const char*)(dC->ptr));
#endif            
            break;
        }
		case LASSO_UINT16: {
#if (LASSO_HOST_ASCII_ENABLE == 1)         
            len = sprintf(dest, "%hu,", *(unsigned short*)dC->ptr);   
#else            
            len = 1;
#endif    
            break;
        }            
        case LASSO_INT16: { 
#if (LASSO_HOST_ASCII_ENABLE == 1)         
            len = sprintf(dest, "%hd,", *(short*)dC->ptr);   
#else              
            len = 2;   
#endif    
            break;
        }
		case LASSO_UINT32: {
#if (LASSO_HOST_ASCII_ENABLE == 1)         
            len = sprintf(dest, "%lu,", *(unsigned long*)dC->ptr);   
#else            
            len = 4;
#endif    
            break;
        }              
		case LASSO_INT32: {
#if (LASSO_HOST_ASCII_ENABLE == 1)         
            len = sprintf(dest, "%ld,", *(long*)dC->ptr);   
#else              
            len = 4;   
#endif    
            break;
        }            
		case LASSO_FLOAT: {
#if (LASSO_HOST_ASCII_ENABLE == 1)         
            len = sprintf(dest, "%f,", *(float*)dC->ptr);   
#else                      
            len = 4;
#endif    
            break;
        }
		case LASSO_DOUBLE: {
#if (LASSO_HOST_ASCII_ENABLE == 1)         
            len = sprintf(dest, "%f,", *(double*)dC->ptr);   
#else                  
            len = 8;
#endif    
            break;
        }
		default: { return ENOTSUP; }
	}
    
#if (LASSO_HOST_ASCII_ENABLE != 1)    
    while (len--) {
        *dest++ = *src++;
    }
#else
    dest += len;
#endif    
    
    *buffer = (uint8_t*)dest;    
    
    return 0;
}


static int32_t lasso_hostGetDatacellNumber(uint8_t** rb, uint8_t* c) {
    const char* cp = (const char*)(*rb);
    uint32_t ui;
#if (LASSO_HOST_ASCII_ENABLE == 1)
    if (sscanf(cp, "%lu", &ui) != 1) {
        return EINVAL;
    }
    
    // advance receiverBuffer (if more data follows after comma)
    *rb = (uint8_t*)strchr(cp, ',') + 1;

    // return value read with sscanf
    *c = (uint8_t)ui;    
#else                        
    c = *cp;  // only 0...255 accepted
    *rb++;
#endif

    return 0;
}


static int32_t lasso_hostGetStrobePeriod(uint8_t** rb, uint16_t* c) {
    const char* cp = (const char*)(*rb);
    uint32_t ui;
#if (LASSO_HOST_ASCII_ENABLE == 1)
    if (sscanf(cp, "%lu", &ui) != 1) {
        return EINVAL;
    }
    
    // advance receiverBuffer (if more data follows after comma)
    *rb = (uint8_t*)strchr(cp, ',') + 1;

    // return value read with sscanf
    *c = (uint16_t)ui;    
#else                        
    ui = ((uint16_t)cp[0] << 8) | cp[1]; // 0...2^16-1 accepted, big-endian   
    *rb += 2;
#endif

    return 0;
}


static int32_t lasso_hostSetDatacellValue(uint8_t** rb, dataCell* dC) {
    const char* cp = (const char*)(*rb);
    
#if (LASSO_HOST_MSGPACK_ENABLE == 1)  
    // todo
#else
    switch (dC->type & LASSO_DATACELL_TYPE_BYTEWIDTH_MASK) {
        case LASSO_BOOL : 
        case LASSO_UINT8 : {
            // read integer 0 or 1 (for bool) or 0 ... 255 (for uint8)
            uint16_t u;

            if (sscanf(cp, "%hu", &u) != 1) {
                return EINVAL;
            }
            
            *(uint8_t*)dC->ptr = (uint8_t)u;
            break;
        }
        
        case LASSO_CHAR : {
            // check incoming string length (might be one for single char)
            uint32_t u = (uint32_t)strlen(cp);
            
            // limit string length if too long for target datacell
            if (u > dC->count) {
                u = dC->count;
                *(char*)(cp + u) = 0;
            }
            
            // read single char or string into datacell
            if (sscanf(cp, "%s", (char*)dC->ptr) != 1) {
                return EINVAL;
            }
            
            // clear remainder of string by writing NULL characters
            u = dC->count - u;
            while (u) {
                *(uint8_t*)(dC->ptr + dC->count - u) = 0;   
                u--;
            }
             
            break;
        }
        
        case LASSO_INT8 : {
            // read 1-Byte int
            int16_t i;

            if (sscanf(cp, "%hi", &i) != 1) {
                return EINVAL;
            }
            
            *(int8_t*)dC->ptr = (int8_t)i;
            break;
        }   
        
        case LASSO_UINT16 : {
            // read 2-Byte uint
            uint16_t u;

            if (sscanf(cp, "%hu", &u) != 1) {
                return EINVAL;
            }
            
            *(uint16_t*)dC->ptr = u;
            break;
        }          
    
        case LASSO_INT16 : {
            // read 2-Byte int
            int16_t i;

            if (sscanf(cp, "%hi", &i) != 1) {
                return EINVAL;
            }
            
            *(int16_t*)dC->ptr = i;
            break;
        }     
        
        case LASSO_UINT32 : {
            // read 4-Byte uint
            uint32_t u;

            if (sscanf(cp, "%lu", &u) != 1) {
                return EINVAL;
            }
            
            *(uint32_t*)dC->ptr = u;
            break;
        }   
        
        case LASSO_INT32 : {
            // read 4-Byte int
            int32_t i;

            if (sscanf(cp, "%li", &i) != 1) {
                return EINVAL;
            }
            
            *(int32_t*)dC->ptr = i;
            break;
        }        
        
    #ifdef ___int64_t_defined
        case LASSO_UINT64: {
            // read 8-Byte uint (endianness of host cpu unimportant since ascii-to-double conversion)
            uint64_t u;
            
            if (sscanf(cp, "%Lu", &u) != 1) {
                return EINVAL;
            }
            
            *(uint64_t*)dC->ptr = u;
            
            break;
        }                                
      
        case LASSO_INT64: {
            // read 8-Byte int (endianness of host cpu unimportant since ascii-to-double conversion)
            int64_t i;
            
            if (sscanf(cp, "%Li", &i) != 1) {
                return EINVAL;
            }
            
            *(int64_t*)dC->ptr = i;
            
            break;
        }         
    #endif
            
        case LASSO_FLOAT :  {
            // read 4-Byte float (endianness of host cpu unimportant since ascii-to-float conversion)
            float fparam;

            if (sscanf(cp, "%f", &fparam) != 1) {
                return EINVAL;
            }
            
            *(float*)dC->ptr = fparam;
            break;
        }
        
        case LASSO_DOUBLE : {
            // read 8-Byte double (endianness of host cpu unimportant since ascii-to-double conversion)
            double dparam;
            
            if (sscanf(cp, "%lf", &dparam) != 1) {
                return EINVAL;
            }
            
            *(double*)dC->ptr = dparam;
            
            break;
        }                            
    
        default: {
            // invalid data type
            return EINVAL;                                    
        }
    }                            
#endif 

    return 0;
}


static void lasso_hostInterpreteControls(void) {
}


static void lasso_hostInterpreteCommand(void) {
#if (LASSO_HOST_MSGPACK_ENABLE == 1)    
	uint32_t nargs;		        // number of arguments in command frame (incl. opcode), and later number of params behind opcode    
#endif    
	int32_t  msg_err = 0;		// error in received message
#if (LASSO_HOST_ASCII_ENABLE == 1)
    uint8_t* receiverBuffer = receiveBuffer;    // make local copy that can be modified
#endif
    uint8_t  opcode;		    // opcode ID
    uint8_t* responseBuffer = response.buffer;
    
	bool tiny_reply = false;	// reply is composed of error code only?
	dataCell* dC = NULL;

	uint8_t  cparam;            // various opcode parameters
	uint16_t sparam;
	uint32_t lparam;    
    bool     bparam;  

#if (LASSO_HOST_MSGPACK_ENABLE == 1)   
	float    fparam;    
#endif 

    response.Bytes_total = 0;

#if (LASSO_HOST_OUTGOING_ENCODING == LASSO_ENCODING_COBS)       
    responseBuffer += 2;
#endif
    
	// handle message
#if (LASSO_HOST_MSGPACK_ENABLE == 1)
    // todo
	PackReaderSetBuffer(&frame_reader, receiveBuffer, response.frame_valid);        // response.frame_valid holds valid Byte length in receive buffer
	PackReaderOpen(&frame_reader, E_PackTypeArray, &nargs);
    
	if (nargs == 2)	{			    // only opcode and array of params expected => 2 arguments
        
		// read opcode from incoming frame
		msg_err = PackReaderGetUnsignedInteger(&frame_reader, &opcode);        
        
		if (msg_err == 0) {                       
			msg_err = PackReaderOpen(&frame_reader, E_PackTypeArray, &nargs);        
        
			if (msg_err == 0) {            
#else
#if (LASSO_HOST_ASCII_ENABLE == 1)            
    msg_err = sscanf((const char*)receiverBuffer++, "%c", &opcode);    
    if (msg_err == 1) {
        msg_err = 0;
        
        msg_err = sprintf((char*)responseBuffer++, "%c", opcode);  
        if (msg_err == 1) {
            msg_err = 0;                                
            
            {      
#else                            
    opcode = *receiverBuffer++;  // max. 256 opcodes allowed here, but some are reserved, e.g. 0 = invalid opcode
    *responseBuffer++ = opcode;    
    {   
        {
            {
#endif                
#endif
            // Unless COBS or ESC encoding has been chosen for command/response and strobe frames:
            // 1) for GET command opcodes (>= 'a'), response & strobe interleaving is impossible
            // 2) for SET command opcodes (>= 'A'), each command deals with the issue differently
            #if (LASSO_HOST_STROBE_ENCODING < LASSO_ENCODING_COBS)  
                if ((lasso_strobing) && (opcode >= 'a')) {
                    return;
                }
            #endif 

            #if (LASSO_HOST_MSGPACK_ENABLE == 1)
    			// prepare reply frame
    			PackWriterSetBuffer(&frame_writer, &responseBuffer, OPENRIO_HOST_TX_BUFFER_SIZE-3);   // check
    			PackWriterOpen(&frame_writer, E_PackTypeArray, 3);
    			PackWriterPutUnsignedInteger(&frame_writer, opcode);    // send opcode back
            #endif
            
				switch (opcode) {
					// LASSO_HOST_GET_x functions
					case LASSO_HOST_GET_PROTOCOL_INFO : {                                                  
                        
                    #if (LASSO_HOST_MSGPACK_ENABLE == 1)      
                        // todo
						PackWriterOpen(&frame_writer, E_PackTypeArray, 1);
						PackWriterPutUnsignedInteger(&frame_writer, LASSO_HOST_PROTOCOL_VERSION);
                    #else
                        #if (LASSO_HOST_ASCII_ENABLE == 1)
                            msg_err = sprintf((char*)responseBuffer, "%lu,", (unsigned long)lasso_protocol_info);
                            if (msg_err > 0) {
                                responseBuffer += msg_err;                             
                            
                                msg_err = sprintf((char*)responseBuffer, "v%d,", (int)LASSO_HOST_PROTOCOL_VERSION);
                                if (msg_err > 0) {
                                    responseBuffer += msg_err;
                                    msg_err = 0;  
                                    break;
                                }
                            }
                            tiny_reply = true;
                            msg_err = ECANCELED;    
                        #else                        
                            *responseBuffer++ = (uint8_t)LASSO_HOST_PROTOCOL_VERSION;
                        #endif
                    #endif
                        
						break;
				    }
                    
                    case LASSO_HOST_GET_TIMING_INFO : {
                        
                    #if (LASSO_HOST_MSGPACK_ENABLE == 1)     
                        // todo
						PackWriterOpen(&frame_writer, E_PackTypeArray, 1);
						PackWriterPutFloat(&frame_writer, (float)LASSO_HOST_TICK_PERIOD_MS);
                    #else
                        #if (LASSO_HOST_ASCII_ENABLE == 1)
                            msg_err = sprintf((char*)responseBuffer, "%.3f,", (float)LASSO_HOST_TICK_PERIOD_MS);
                            if (msg_err > 0) {
                                responseBuffer += msg_err;
                                
                                msg_err = sprintf((char*)responseBuffer, "%d,", (int)LASSO_HOST_COMMAND_TIMEOUT_TICKS);
                                if (msg_err > 0) {
                                    responseBuffer += msg_err;                                
                                
                                    msg_err = sprintf((char*)responseBuffer, "%d,", (int)LASSO_HOST_RESPONSE_LATENCY_TICKS);
                                    if (msg_err > 0) {
                                        responseBuffer += msg_err;                                
        
                                        msg_err = sprintf((char*)responseBuffer, "%d,", (int)LASSO_HOST_STROBE_PERIOD_MIN_TICKS);
                                        if (msg_err > 0) {
                                            responseBuffer += msg_err;                                          
                                        
                                            msg_err = sprintf((char*)responseBuffer, "%d,", (int)LASSO_HOST_STROBE_PERIOD_MAX_TICKS);
                                            if (msg_err > 0) {
                                                responseBuffer += msg_err;                               
            
                                                msg_err = sprintf((char*)responseBuffer, "%d,", (int)lasso_strobe_period);
                                                if (msg_err > 0) {
                                                    responseBuffer += msg_err;   
                                                    
                                                    msg_err = 0;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }                                                                                
                            }
                            tiny_reply = true;
                            msg_err = ECANCELED;                                
                        #else                        
                            *(float*)responseBuffer++ = (float)LASSO_TICK_HOST_PERIOD_MS;
                        #endif
                    #endif                        
                        
                        break;   
                    }
                    
					case LASSO_HOST_GET_DATACELL_COUNT : {
                        
                    #if (LASSO_HOST_MSGPACK_ENABLE == 1) 
                        // todo
						 PackWriterOpen(&frame_writer, E_PackTypeArray, 1);
						 PackWriterPutUnsignedInteger(&frame_writer, dataCellCount);
                    #else
                        #if (LASSO_HOST_ASCII_ENABLE == 1)
                            msg_err = sprintf((char*)responseBuffer, "%d,", (int)dataCellCount);
                            if (msg_err > 0) {
                                responseBuffer += msg_err;
                                msg_err = 0;
                                break;
                            }
                            tiny_reply = true;
                            msg_err = ECANCELED;
                        #else
                            *responseBuffer++ = (uint8_t)dataCellCount;
                        #endif
                    #endif
                        
					    break;
					}
                    
					case LASSO_HOST_GET_DATACELL_PARAMS : {
                        
                    #if (LASSO_HOST_MSGPACK_ENABLE == 1)     
                        // todo
						if (nargs == 1) {
							msg_err = PackReaderGetUnsignedInteger(&frame_reader, &lparam);
							cparam = (uint8_t)lparam;
							if (msg_err == 0) {
								dC = lasso_hostSeekDataCell(cparam, &lparam);
                            }
                        }
                    #else
                        msg_err = lasso_hostGetDatacellNumber(&receiverBuffer, &cparam);
                        if (msg_err) {
                            tiny_reply = true;
                            break;
                        }

                        dC = lasso_hostSeekDatacell(cparam, &lparam);
                    #endif
                                
                        if (dC) {
                    #if (LASSO_HOST_MSGPACK_ENABLE == 1)   
                        // todo
							PackWriterOpen(&frame_writer, E_PackTypeArray, 6);
							PackWriterPutUnsignedInteger(&frame_writer, dC->type);
							PackWriterPutUnsignedInteger(&frame_writer, dC->count);
							PackWriterPutString(&frame_writer, dC->name);
                            PackWriterPutString(&frame_writer, dC->unit);
                            PackWriterPutUnsignedInteger(&frame_writer, dC->update_rate);
							PackWriterPutUnsignedInteger(&frame_writer, lparam);
                    #else
                            lasso_copyDatacellParams(dC, &responseBuffer, lparam);
                    #endif   
                        }
                        else {
                            tiny_reply = true;
    						msg_err = EINVAL;                        
                        }
                        
                        break;
					}
                    
					case LASSO_HOST_GET_DATACELL_VALUE : {
                        
                    #if (LASSO_HOST_MSGPACK_ENABLE == 1)    
						if (nargs >= 1) {
							msg_err = PackReaderGetUnsignedInteger(&frame_reader, &lparam);
							cparam = (uint8_t)lparam;
							if (msg_err == 0) {
								dC = lasso_hostSeekDataCell(cparam, &lparam);
                            }
                        }
                    #else
                        msg_err = lasso_hostGetDatacellNumber(&receiverBuffer, &cparam);
                        if (msg_err) {
                            tiny_reply = true;
                            break;
                        }

                        dC = lasso_hostSeekDatacell(cparam, &lparam);     
                    #endif
                    
                        if (dC) {
                    #if (LASSO_HOST_MSGPACK_ENABLE == 1)   
                        // todo
    						PackWriterOpen(&frame_writer, E_PackTypeArray, 1);                                    
    						switch (dC->type & LASSO_HOST_TYPE_BYTEWIDTH_MASK)
    						{
    							case LASSO_BOOL:   { PackWriterPutBoolean(&frame_writer, *(bool*)(dC->ptr)); break; }
    							case LASSO_CHAR:   { PackWriterPutString(&frame_writer, (const char*)(dC->ptr)); break; }
    							case LASSO_UINT8:  { PackWriterPutUnsignedInteger(&frame_writer, *(unsigned char*)(dC->ptr)); break; }
    							case LASSO_INT8:   { PackWriterPutSignedInteger(&frame_writer, *(signed char*)(dC->ptr)); break; }
    							case LASSO_UINT16: { PackWriterPutUnsignedInteger(&frame_writer, *(unsigned short*)(dC->ptr)); break; }
    							case LASSO_INT16:  { PackWriterPutSignedInteger(&frame_writer, *(signed short*)(dC->ptr)); break; }
    							case LASSO_UINT32: { PackWriterPutUnsignedInteger(&frame_writer, *(unsigned long*)(dC->ptr)); break; }
    							case LASSO_INT32:  { PackWriterPutSignedInteger(&frame_writer, *(signed long*)(dC->ptr)); break; }
    							case LASSO_FLOAT:  { PackWriterPutFloat(&frame_writer, *(float*)(dC->ptr)); break; }
    							case LASSO_DOUBLE: { PackWriterPutFloat(&frame_writer, *(double*)(dC->ptr)); break; }
    							default: { tiny_reply = true; msg_err = ENOTSUP; }
    						}
                    #else
                            if (lasso_copyDatacellValue(dC, &responseBuffer) != 0) {
                                tiny_reply = true;
    							msg_err = EINVAL;
                            }                                    
                    #endif
                        }
                        else {
                            tiny_reply = true;
							msg_err = EINVAL;
                        }
                        
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
                        // strobing on : unless COBS or ESC strobe encoding, this command does not send any reply; however, effect can easily be seen on client side
                        // strobing off: acknowledgement is sent (tiny reply)
                        
                        // read in period from command's parameter list
                    #if (LASSO_HOST_MSGPACK_ENABLE == 1)
						if (nargs >= 1) {
                            msg_err = PackReaderGetUnsignedInteger(&frame_reader, &lparam)
                            if (msg_err == 0) {
                                sparam = (uint16_t)lparam;
                            }
                        }
                        else {
                            msg_err = ECANCELED;
                        }
                    #else
                        msg_err = lasso_hostGetStrobePeriod(&receiverBuffer, &sparam);
                    #endif                    
                        // try to set new strobe period
                        if (msg_err == 0) {
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
                            }
                        }
                    
                    #if (LASSO_HOST_STROBE_ENCODING < LASSO_ENCODING_COBS)  // no response & strobe interleaving possible                        
                        if (lasso_strobing) {
                            return;
                        }
                    #endif
                        
                        tiny_reply = true;
                        break;
                    }                        
                    
                    case LASSO_HOST_SET_DATASPACE_STROBE : {
                        // strobing on : unless COBS or ESC strobe encoding, this command does not send any reply; however, effect can easily be seen on client side
                        // strobing off: same
                        
                        // get flag from command's parameter list
                    #if (LASSO_HOST_MSGPACK_ENABLE == 1)   
						if (nargs >= 1) {
							msg_err = PackReaderGetUnsignedInteger(&frame_reader, &lparam);
                        }
                        else {
                            msg_err = ECANCELED;
                        }
                    #else
                        #if (LASSO_HOST_ASCII_ENABLE == 1)
                            if (sscanf((const char*)receiverBuffer, "%lu", &lparam) != 1) {
                                msg_err = EINVAL;
                            }
                        #else
                            lparam = *(uint32_t*)receiverBuffer;
                        #endif
                    #endif                        
                        
                        // switch strobing on/off
                        if (msg_err == 0) {
                            if (lparam) {
                                if (!lasso_strobing) {
                                    strobe.countdown = 1;   // start strobing immediately
                                }
                                lasso_strobing = true;
                            }
                            else {
                                lasso_strobing  = false;
                                lasso_advertise = false;    // command also used to stop advertising
                            }                    
                            if (actCallback) {
                                actCallback(lasso_strobing);
                            }
                        }
                    
                    #if (LASSO_HOST_STROBE_ENCODING < LASSO_ENCODING_COBS)  // no response & strobe interleaving possible
                        return;    
                    #endif
                    
                        tiny_reply = true;
                        break;
                    }
                    
					case LASSO_HOST_SET_DATACELL_STROBE : {
                        // strobing on : not possible -> this command requires strobing to be off since it changes the strobe length
                        // strobing off: acknowledge is sent (tiny reply)
                        if (lasso_strobing) {
                            return;
                        }
                        tiny_reply = true;      
                        
                    #if (LASSO_HOST_MSGPACK_ENABLE == 1)   
                        // todo
						if (nargs == 2) {
							msg_err = PackReaderGetUnsignedInteger(&frame_reader, &lparam);
							cparam = (uint8_t)lparam;
							if (msg_err == 0) {
								msg_err = PackReaderGetBoolean(&frame_reader, &bparam);
								if (msg_err == 0) {
									dC = lasso_hostSeekDataCell(cparam, &lparam);
                                }
                            }
                        }
                        else {
                            msg_err = ECANCELED;
                        }
                    #else                        
                        msg_err = lasso_hostGetDatacellNumber(&receiverBuffer, &cparam);
                        if (msg_err) {
                            break;
                        }

                        dC = lasso_hostSeekDatacell(cparam, &lparam);     
                    #endif                        
                                        
                        if (dC) {
                    #if (LASSO_HOST_MSGPACK_ENABLE == 0)  
                        #if (LASSO_HOST_ASCII_ENABLE == 1)
                            if (sscanf((char*)receiverBuffer, "%lu", &lparam) != 1) {
                                msg_err = EINVAL;
                                break;
                            }
                            bparam = lparam != 0;
                        #else
                            bparam = *receiverBuffer;
                        #endif                                
                    #endif
          
                            lparam = dC->type & LASSO_DATACELL_ENABLE_MASK;
							if (bparam)	{
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
								if (lparam)	{
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
							msg_err = EINVAL;
                        }
                        
                        break;                          
					}
                    
                    case LASSO_HOST_SET_DATACELL_VALUE : {
                        // strobing on : unless COBS or ESC strobe encoding, this command does not send any reply; however, effect can easily be seen on client side
                        // strobing off: acknowledgement is sent (tiny reply)
                    
                    #if (LASSO_HOST_MSGPACK_ENABLE == 1)     
                        // todo
						if (nargs >= 2) {
							msg_err = PackReaderGetUnsignedInteger(&frame_reader, &lparam);
							cparam = (uint8_t)lparam;
                            // must determine which data type is expected...
							if (msg_err == 0) {
								msg_err = PackReaderGetBoolean(&frame_reader, &bparam);
								if (msg_err == 0) {
									dC = lasso_hostSeekDataCell(cparam, &lparam);
                                }
                            }
                        }
                        else {
                            msg_err = ECANCELED;
                        }
                    #else                
                        msg_err = lasso_hostGetDatacellNumber(&receiverBuffer, &cparam);
                        if (msg_err == 0) {
                            dC = lasso_hostSeekDatacell(cparam, &lparam);     
                        }
                    #endif    

                        if (dC) {
                            msg_err = lasso_hostSetDatacellValue(&receiverBuffer, dC);
                            
                            if (dC->onChange) {
                                dC->onChange((void * const)dC->ptr);
                            }
                        }        
                        
                    #if (LASSO_HOST_STROBE_ENCODING < LASSO_ENCODING_COBS)  // no response & strobe interleaving possible
                        if (lasso_strobing) {
                            return;
                        }
                    #endif
                        
                        tiny_reply = true;
                        break;
                    }
                    
					default : {     // e.g. LASSO_HOST_INVALID_OPCODE
						tiny_reply = true;
						msg_err = EOPNOTSUPP;
					}
				}
			}

        #if (LASSO_HOST_MSGPACK_ENABLE == 1)                             
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
                responseBuffer = response.buffer + 1;                       // write only error code behind opcode
			}
            #if (LASSO_HOST_ASCII_ENABLE == 1)
                msg_err = sprintf((char*)responseBuffer, "%ld", msg_err);
                responseBuffer += msg_err;
            #else
                *responseBuffer++ = (uint8_t)msg_err;            
            #endif
            
            response.Bytes_total = responseBuffer - response.buffer;        // includes COBS header if any
        #endif            
            
        #if (LASSO_HOST_RESPONSE_CRC_ENABLE == 1)
            lasso_hostAppendCRC(response.buffer, response.Bytes_total);            
            responseBuffer       += LASSO_HOST_CRC_BYTEWIDTH;
			response.Bytes_total += LASSO_HOST_CRC_BYTEWIDTH;
        #endif
        
        #if (LASSO_HOST_RESPONSE_ENCODING == LASSO_ENCODING_RN)
            *responseBuffer++ = 0x0D;   // '\r'
            *responseBuffer   = 0x0A;   // '\n'
            response.Bytes_total += 2;
        #endif    
		}
	}
}


//-------------------------//
// Module public functions //
//-------------------------//

// registers an arbitrary data cell, specified by its "type", length "count", RAM start address "ptr", "name" and "unit"
int32_t lasso_hostRegisterDataCell(uint16_t type, uint16_t count, const void* ptr, const char* const name, const char* const unit, const lasso_chgCallback onChange, uint32_t update_rate) {
	dataCell* dC = LASSO_HOST_MALLOC(sizeof(dataCell));
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
    dC->update_rate = update_rate;
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

#if (LASSO_HOST_TIMESTAMP == 1)
int32_t lasso_hostRegisterTimestamp(const char* const name, const char* const unit, const lasso_chgCallback onChange) {
    return lasso_hostRegisterDataCell(LASSO_UINT32 | LASSO_DATACELL_ENABLE, 1, (void*)&lasso_timestamp, name, unit, onChange, 1);
}
#endif

/*
 * Prepare host memory spaces (strobe and response) for serial transmission.
 *
 * Assumes that:
 * - all variables to be sampled have been registered with lasso_hostRegisterDataCell()
 * - strobe.Bytes_max and strobe.Bytes_total are known
 *
 * More than the regular minimum space is allocated if:
 * - message packing is enabled for host responses
 * - CRC inclusion is enabled for host responses and/or strobe
 * - an encoding scheme is selected
 * 
 * E.g. for msgpack'ed responses, CRC and COBS encoding scheme, memory space is
 * adjusted as follows:
 *
 * Additional space for header:
 * - 2 Bytes for COBS header: 0x00 (frame delimiter) and first COBS code
 * - 1 Byte for the "invalid" MessagePack code 0xC1 (indicating to lasso client that message shall not be decoded with MessagePack)
 *
 * Additional space for footer:
 * - 2 Bytes for 16 bit CRC (actually depending on CRC Byte width specified by user)
 * - 1 Byte for COBS phantom delimiter Byte
 *
 * Finally, total memory space requirements are rounded to the next alignment boundary.
 */
int32_t lasso_hostRegisterMEM(void) {
    #if (LASSO_HOST_MSGPACK_ENABLE == 1)
        strobe.Bytes_max += 1;                                  // an "invalid" MessagePack code is added to the strobe packet
        strobe.Bytes_total += 1;
    #endif
    
    #if (LASSO_HOST_STROBE_CRC_ENABLE == 1) // check: for COBS, one CRC each 251 Bytes, right?
        strobe.Bytes_max += LASSO_HOST_CRC_BYTEWIDTH;           // add space for CRC to end of strobe packet
        strobe.Bytes_total += LASSO_HOST_CRC_BYTEWIDTH;
    #endif
    
    #if (LASSO_HOST_RESPONSE_CRC_ENABLE == 1)
        response.Bytes_max += LASSO_HOST_CRC_BYTEWIDTH;         // add space for CRC to end of response packet
    #endif    
    
    #if (LASSO_HOST_RESPONSE_ENCODING == LASSO_ENCODING_COBS)   // consistent overhead Byte stuffing (overhead = 3 Bytes)
	    strobe.Bytes_max += 3;
        strobe.Bytes_total += 3;
	    response.Bytes_max += 3;        
    #else        
        #if (LASSO_HOST_RESPONSE_ENCODING == LASSO_ENCODING_RN)
            response.Bytes_max += 2;          
        #endif
            
        #if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_RN)   // packet end delimiter '\r\n', no escaping, 2 Bytes overhead
            stobe.Bytes_max += 2;  
            strobe.Bytes_total += 2;
        #endif
        // currently no implementation for other escaping techniques (ESC ...)
    #endif
            
    if (strobe.Bytes_max & (LASSO_MEMORY_ALIGN - 1)) {
        strobe.Bytes_max &= ~(LASSO_MEMORY_ALIGN - 1);
        strobe.Bytes_max += LASSO_MEMORY_ALIGN;
    }

    if (response.Bytes_max & (LASSO_MEMORY_ALIGN - 1)) {
        response.Bytes_max &= ~(LASSO_MEMORY_ALIGN - 1);
        response.Bytes_max += LASSO_MEMORY_ALIGN;
    }
    
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
    
    receiveBuffer = (uint8_t*)LASSO_HOST_MALLOC(LASSO_HOST_RX_BUFFER_SIZE);
    if (receiveBuffer == NULL) {
        return ENOMEM;
    }   
    
    return 0;
}


int32_t lasso_hostRegisterCOM(lasso_comSetup cS, lasso_comCallback cC, lasso_offCallback oC, lasso_actCallback aC, lasso_perCallback pC) {
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
    
    if (oC) {
        offCallback = oC;
    }
    
    if (aC) {
        actCallback = aC;
    }

    if (pC) {
        perCallback = pC;
    }
    
    return 0;
}


int32_t lasso_hostRegisterCRC(lasso_crcCallback cC) {
    if (cC) {
        crcCallback = cC;
    }
    else {
        return EINVAL;
    }
    
    return 0;    
}
      

int32_t lasso_hostReceiveByte(uint8_t b) {
    if (receiveBufferIndex < LASSO_HOST_RX_BUFFER_SIZE) {
        
#if (LASSO_HOST_COMMAND_ENCODING == LASSO_ENCODING_RN)        
        if (b == '\n') {
            if (receiveBufferIndex == 0) {   
                return ENODATA;
            }
            if (receiveBuffer[receiveBufferIndex - 1] == '\r') {
                // 1) terminate string by NULL character or whitespace character (0x20)
                //    -> does not seem to be necessary for sscanf (works with '\r')                
                //    -> however, in cases such as "LASSO_HOST_SET_DATACELL_VALUE" a terminating NULL character is useful
                // 2) CRC check
                //    -> not used for RN encoding
                receiveBuffer[receiveBufferIndex - 1] = 0;
                response.valid = receiveBufferIndex;
                receiveBufferIndex = 0;
                return 0;
            }
            else {
                receiveBufferIndex = 0;
                return EILSEQ;     // '\n' not allowed without prior '\r'
            }
        }
#else
    // todo
#endif

        if (response.valid == 0) {   // only one command can be handled at a time       
            receiveBuffer[receiveBufferIndex++] = b;
            receiveTimeout = LASSO_HOST_COMMAND_TIMEOUT_TICKS;
        }
        else {
            receiveBufferIndex = 0;
            return EOVERFLOW;
        }
    }
    else {
        receiveBufferIndex = 0;
        return ENOSPC;
    }
    
    return 0;
}


/*
bool openRIO_hostTransmitIdle(void);
#define OPENRIO_HOST_SSR TO_STR(0x8A004 + 0x20 * OPENRIO_HOST_SCI_CH)	// Link to SCI SSR register for configured SCI port
__asm("_openRIO_hostTransmitIdle: \n" \
	  "PUSH R2					\n" \

	  // DMA_to_SCI_done?
	  "MOV.L #_DMA_to_SCI_done, R1 \n" \
	  "MOVU.B [R1], R2			\n" \
	  "CMP #0, R2				\n" \
	  "BZ 1f					\n" \

	  // SCI TEND?
	  "MOV.L #" OPENRIO_HOST_SSR ", R2 \n" \
	  "MOVU.B [R2], R2			\n" \
	  "BTST #2, R2				\n" \
	  "BZ 1f					\n" \

	  // DMA_to_SCI_done == true -> DMA_to_SCI_done = false
	  "MOV.B #0, [R1]			\n" \

	  // exit true
	  "MOV.L #1, R1				\n" \
	  "POP R2					\n" \
	  "RTS						\n" \

	  // exit false
	  "1:						\n" \
	  "MOV.L #0, R1				\n" \
	  "POP R2					\n" \
	  "RTS");
*/

// tries to transmit a frame, if scheduled; ptr is address of dataFrame struct, either strobe or response
// returns true if frame is being sent, false otherwise
    // and 256th Byte in variable space (those will be crushed by COBS_encode)
                
				// if send first frame successful:
					// if strobe.on:
						// store 256th Byte definitely in strobe.backup
						// decrement strobe.Byte_count by 253
						// increment strobe.frame by 253
				// else:
					// strobe.on = true
					// store 3rd Byte definitely in strobe.backup (1st frame has not been COBS_encoded yet)
            
            
static bool lasso_hostTransmitDataFrame(dataFrame* ptr) {
    uint8_t* frame = ptr->frame;
    uint32_t num = ptr->Byte_count;
#if (LASSO_HOST_RESPONSE_ENCODING == LASSO_ENCODING_COBS)     // if COBS selected for response frames, COBS will also be used for strobe frames
    uint8_t  COBS_backup;
#endif
    
    if (num > 0) {
        
    #if (LASSO_HOST_RESPONSE_ENCODING == LASSO_ENCODING_COBS)                   
        if (num > 253) {
            num = 253;
        }

        COBS_backup = frame[255];           // temporarily save next COBS backup, frame[255] will be crushed by COBS_encode()
        
        if (frame[0] != 0x00) {             // encode only if not already done
            frame[2] = ptr->COBS_backup;
            COBS_encode(frame, num);        // "frame" holds the 2 Bytes COBS header, and "num" is the number of payload Bytes
        }
        
        if (comCaller(frame, num + 3) == 0) {        
            ptr->COBS_backup = COBS_backup;
    #else
        if (num > LASSO_HOST_MAX_FRAME_SIZE) {
            num = LASSO_HOST_MAX_FRAME_SIZE;
        }            
        
        if (comCallback(frame, num) != EBUSY) {        
    #endif    
       
            ptr->frame      += num;
            ptr->Byte_count -= num;
            return true;
        }
    }
    
    return false;
}

/*
__asm("_openRIO_hostTransmitDataFrame: \n" \
		"PUSHM R2-R4			\n" \

		// check whether at least one frame is scheduled (strobe/reponse.byte_count > 0)
		"MOV.L 12[R1], R2		\n" \
		"CMP #0, R2				\n" \
		"BZ 2f					\n" \

		// if yes, check whether it can be sent
		"PUSH R1				\n" \
		"BSR _openRIO_hostTransmitIdle \n" \
		"CMP #0, R1				\n" \
		"POP R1					\n" \
		"BZ 2f					\n" \

		// frame can be sent; check whether more frames remain, limit length of current frame to 253 (R2) and save "extended" indicator in R3
		"CMP #253, R2			\n" \
		"BGT 11f				\n" \
		"MOV.L #0, 12[R1]		\n" \
		"MOV.L #0, R3			\n" \
		"BRA 12f				\n" \
		"11:					\n" \
		"MOV.L #253, R2			\n" \
		"MOV.L #1, R3			\n" \
		"12:					\n" \

		// fetch backup-ed Byte from strobe/response.COBS_backup (crushed by previous COBS_encode operation)
		"MOVU.B 2[R1], R4		\n" \
		"PUSH R1				\n" \
		"MOV.L 4[R1], R1		\n" \

		// restore backup-ed Byte in frame buffer
		"MOV.B R4, 2[R1]		\n" \

		// send next frame, after having saved next backup temporarily in R4
		"MOV.B 255[R1], R4		\n" \
		"PUSHM R1-R2			\n" \
		"BSR _COBS_encode		\n" \
		"POPM R1-R2				\n" \
		"BSR _lasso_hostTriggerTransmission \n" \

		// check if more frames are to come
		"POP R1					\n" \
		"MOV.L 12[R1], R2		\n" \
		"CMP #0, R2				\n" \
		"BZ 1f					\n" \

		// record 256th Byte definitely in strobe/response.COBS_backup (stored so far only in R4)
		"MOV.B R4, 2[R1]		\n" \

		// increment/decrement strobe/response.chunk and strobe/response.Byte_count resp.
		"MOV.L 8[R1], R2		\n" \
		"ADD #253, R2			\n" \
		"MOV.L R2, 8[R1]		\n" \
		"MOV.L 12[R1], R2		\n" \
		"SUB #253, R2			\n" \
		"MOV.L R2, 12[R1]		\n" \

		// exit true (sending frame)
		"1:						\n" \
		"MOV.L #1, R1			\n" \
		"RTSD #12, R2-R4		\n" \

		// exit false (nothing to send or cannnot be sent)
		"2:						\n" \
		"MOV.L #0, R1			\n" \
		"RTSD #12, R2-R4");

*/


// provides strobe synchronization to external data    
void lasso_hostSetBuffer(uint8_t* buffer) {
    strobe.buffer = buffer;
}


// provides strobe synchronization to external events
void lasso_hostCountdown(uint16_t count) {
    if (count > strobe.countdown) {        
        strobe.countdown = 0;
    }
    else {
        strobe.countdown -= count;
    }
}

    
// for RXv2 optimized version            
#define LASSO_HOST_BROADCAST_PERIOD TO_STR(LASSO_HOST_STROBE_PERIOD_TICKS)
#define LASSO_HOST_RESPONSE_LATENCY TO_STR(LASSO_HOST_RESPONSE_LATENCY_TICKS)
            
void lasso_hostHandleCOM(void)
{
    // verify memory allocation of receiveBuffer (otherwise, lasso_hostRegisterMEM() has not been called yet)
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
            strobe.countdown = (uint16_t)(LASSO_HOST_ADVERTISE_PERIOD_TICKS);
            
            strobe.frame = (uint8_t*)&signature;        // reload buffer start
            strobe.Byte_count = sizeof(signature);      // reload Byte count for transmission      
        }
    }
    else
    
    // Pseudo-code for strobe processing:
    // if global strobe enable:
    	// strobe.countdown--
    		// if strobe.countdown == 0:
    			// strobe.countdown = strobe.period
    			// if still strobe (=strobe.Byte_count > 0):
                    // overdrive = true
    				// strobe.valid = false
    			// else:
                    // strobe.valid = true (automatic reset maybe not desired)
    				// sample variable space    
    				// record length of variable space in strobe.Byte_count

    				// if COBS encoding selected:
                        // take note of 3rd Byte in variable space (will be crushed by COBS_encode)
    // continue with response processing    
    
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
                //strobe.valid = true;          // automatic reset of this flag might not be desired
                            
                lasso_hostSampleDataCells();

                strobe.frame = strobe.buffer;             // reload buffer start
                strobe.Byte_count = strobe.Bytes_total;   // reload Byte count for transmission
                            
            #if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_COBS)
                strobe.COBS_backup = strobe.buffer[2];
            #endif
            }
        }
    }
        
    /*
	// launch a new strobe every strobe.period (= LASSO_HOST_STROBE_PERIOD_TICKS ms), counted by strobe.countdown
	asm volatile("MOV.L #_strobe, R1	\n" \
				"MOVU.W [R1], R2		\n" \
				"SUB #1, R2 			\n" \
				"MOV.W R2, [R1]			\n" \
				"BNZ 3f					\n" \
				"MOV.W # " LASSO_HOST_STROBE_PERIOD ", [R1] \n" \

				// still strobing (indicated by strobe.Byte_count > 0)?
				"MOV.L 12[R1], R2 		\n" \
				"CMP #0, R2				\n" \
				"BZ 1f					\n" \

				// if still in the process of strobing, set strobe.valid false and exit
				"MOV.B #0, 3[R1] 		\n" \
				"BRA 3f					\n" \

				// if not strobing, prepare next strobe
				"1:						\n" \

				// copy length of current strobe data space "strobe.Bytes_total" to "strobe.Byte_count"
				"MOV.L 20[R1], R2	    \n" \
				"MOV.L R2, 12[R1]		\n" \

                // save strobe.buffer for later
				"MOV.L 4[R1], R1		\n" \
				"PUSH R1				\n" \
        
				// launch lasso_hostSampleDataCells
				"BSR _lasso_hostSampleDataCells \n" \

				// save 3rd Byte of first frame in strobe.COBS_backup (see further below for more info; usually, 256th Byte is saved)
				"POP R1					\n" \
				"MOVU.B 2[R1], R2		\n" \
				"MOV.L #_strobe, R1	    \n" \
				"MOV.B R2, 2[R1]		\n" \
				"3:" ::: "r1", "r2");
    */
    
    // Pseudo-code for response processing:
	// response.countdown--
		// if response.countdown == 0:
			// response.countdown = response.period
			// if finished sending last response (=response.Byte_count == 0):
				// if valid request received (=response.valid > 0):
                    // if CRC of request ok (=0):
                        // if first Byte in receiveBuffer == 0xC1:
                            // interprete controls
                        // else
                            // interprete command  
				            // record length of response in response.Byte_count

				            // if COBS encoding selected:
                                // take note of 3rd Byte in response (will be crushed by COBS_encode)
                
                            // set response.valid = 0
                    // else (CRC not ok):
                        // send adequate error message
    // continue with strobe and response frame transmission    
    
    response.countdown--;
    if (response.countdown == 0) {
        response.countdown = (uint16_t)(LASSO_HOST_RESPONSE_LATENCY_TICKS);
          
        if (response.Byte_count == 0) {
            if (response.valid > 0) {
                #if (LASSO_HOST_COMMAND_CRC_ENABLE == 1)
                if (crcCaller(receiveBuffer, response.valid) == 0) {
                #else
                {
                #endif
                    if (receiveBuffer[0] == 0xC1) {
                        lasso_hostInterpreteControls();
                    }
                    else {
                        lasso_hostInterpreteCommand();

                        response.frame = response.buffer;               // reload buffer start
                        response.Byte_count = response.Bytes_total;     // reload Byte count for transmission                                    
                        
                    #if (LASSO_HOST_RESPONSE_ENCODING == LASSO_ENCODING_COBS)
                        response.COBS_backup = response.buffer[2];
                    #endif             
                    
                        response.valid = 0;
                    }
                }
                #if (LASSO_HOST_COMMAND_CRC_ENABLE == 1)                
                else {
                    while(1);       // to be changed later
                }
                #endif
            }
        }
    }
        
    /*
	// launch a new check on received commands every response.period (= LASSO_HOST_RESPONSE_LATENCY_MS/LASSO_UPDATE_RATE_MS ms), counted by response.countdown
	asm volatile("MOV.L #_response, R1	\n" \
				"MOVU.W [R1], R2		\n" \
				"SUB #1, R2 			\n" \
				"MOV.W R2, [R1]			\n" \
				"BNZ 4f					\n" \
				"MOV.W # " LASSO_HOST_RESPONSE_LATENCY ", [R1] \n" \

				// if still in the process of sending a response (indicated by response.Byte_count > 0), exit, otherwise continue
				"MOV.L 12[R1], R2 		\n" \
				"CMP #0, R2				\n" \
				"BNZ 4f					\n" \

				// check whether a message frame has been received (response.overdrive_valid > 0)
				"MOVU.B 3[R1], R2		\n" \
				"CMP #0, R2				\n" \
				"BZ 4f					\n" \

				// init pointer to receiveBuffer
				"MOV.L #_receiveBuffer, R1 \n" \
				"MOV.L [R1], R1			\n" \
				"PUSH R1				\n" \

				// verify 16-bit CRC is zero
				"BSR _CRC_get			\n" \
				"CMP #0, R1				\n" \
				"POP R1					\n" \
				"BZ 2f					\n" \

				// stop in infinite loop (change later)
				"11:					\n" \
				"BRA 11b				\n" \

				// if CRC check successful, interprete controls or command
				"2:						\n" \
				"MOVU.B [R1], R1		\n" \
				"CMP #0xC1, R1			\n" \
				"BNE 3f					\n" \

				// controls ... (requiring no response)
				"BSR _openRIO_hostInterpreteControls \n" \
				"BRA 4f					\n" \

				// ... or command (requiring a response)
				"3:						\n" \
				"BSR _openRIO_hostInterpreteCommand \n" \

				// copy addr of first frame (= *response.buffer) to response.buffer (removed ... done once in openRIO_hostRegisterHeap)
				"MOV.L #_response, R1	\n" \
				"MOV.L #_response.buffer, R2 \n" \
				"MOV.L [R2], R2			\n" \

				// save 3rd Byte of first frame in response.COBS_backup (see further below for more info; usually, 256th Byte is saved)
				"MOVU.B 2[R2], R2		\n" \
				"MOV.B R2, 2[R1]		\n" \

				// copy length of data (= response.Bytes_total) to response.Byte_count
				"MOV.L #_response.Bytes_total, R2 \n" \
				"MOV.L [R2], R2			\n" \
				"MOV.L R2, 12[R1]		\n" \

				// clear response.valid
				"MOV.B #0, 3[R1]		\n" \
				"4:" ::: "r1", "r2");
    */
        
    // try sending broadcast frame
    // if returns "0" (=not sending broadcast frame):
        // try sending response frame
    // exit

    if (!lasso_hostTransmitDataFrame(&strobe)) {
        lasso_hostTransmitDataFrame(&response);
    }
    /*
	asm volatile("MOV.L #_strobe, R1 \n" \
				"BSR _lasso_hostTransmitDataFrame	\n"
				"CMP #0, R1 \n" \
				"BNZ 1f		\n" \
				"MOV.L #_response, R1 \n" \
				"BSR _lasso_hostTransmitDataFrame \n" \
				"1:" ::: "r1");
    */
    
    lasso_timestamp++;
    
    if (offCallback) {
        offCallback();
    }
}
#endif
