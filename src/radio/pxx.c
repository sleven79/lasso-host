/******************************************************************************/
/*                                                                            */
/*  \file       pxx.c                                                         */
/*  \date       Sep 2018 -                                                    */
/*  \author     Severin Leven                                                 */
/*                                                                            */
/*  \brief      PXX communication protocol with radio TX module               */
/*                                                                            */
/*  This file is part of the Lasso host library. Lasso is a configurable and  */
/*  efficient mechanism for data transfer between a host (server) and client. */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  Target CPU: any 32-bit                                                    */
/*  Ressources: CPU (for com/timer ressources see target-specific module)     */
/*                                                                            */
/******************************************************************************/


/******************************************************************************/
/*                                                                            */
/* FrSky PXX protocol (PCM) format specification                              */
/* supported by FrSky XJT and R9M modules                                     */
/* v1.1 (knowledge SLE)                                                       */
/*                                                                            */
/* 20 Bytes, each bit is pulse coded, transmitted lsb first                   */
/* 0:  8us low, 8us high (16us pulse length)                                  */
/* 1: 16us low, 8us high (24us pulse length)                                  */
/*                                                                            */
/* Bit stuffing: after five 1-bits, stuff 0-bit.                              */
/*                                                                            */
/* Total bitstream length:                                                    */
/* header: 18 bits                                                            */
/* body: 432 bits (worst case with all zeros)                                 */
/* tail: 18 bits                                                              */
/* total: 468 bits (58.5 Bytes)                                               */
/*                                                                            */
/* Bitstream is transmitted every 9ms. If transmitting 16 channels instead    */
/* of 8, two bitstreams need to be transmitted (=18ms total cycle time).      */
/* A "high" baudrate mode is available, the cycle time decreases to 4ms.      */
/*                                                                            */
/* We implement 125kBaud bitstream on SPI MOSI                                */
/* 0: 01                                                                      */
/* 1: 001                                                                     */
/*                                                                            */
/* Hardware:                                                                  */
/* 0: strong drive, open drain/collector pull                                 */
/* 1: weak drive, resistor pull                                               */
/*                                                                            */
/* PPM values are coded as 0..2047 for ch1-8 and 2048...4095 for ch9-16,      */
/* with middle value of 1024 (ch1-8) resp. 3072 (ch9-16).                     */
/* The range can be mapped from the usual 800-2200us by multiplying by        */
/* 512/682 (proposal).                                                        */
/*                                                                            */
/* For failsafe channel values:                                               */
/* - ppm 2000 -> pxx failsafe channel hold                                    */
/* - ppm 2001 -> pxx failsafe channel no pulse                                */
/*                                                                            */
/* Byte format:                                                               */
/* 1) Header (0x7E, no bit stuffing)                                          */
/* 2) Receiver (RX) number (for bind process)                                 */
/* 3) Flag 1 (actions of all listening RXs):                                  */
/*    b0: set RX number (bind)                                                */
/*    b1...b3: set failsafe positions ch1-8/9-16                              */
/*    b4: set failsafe                                                        */
/*    b5: rangecheck                                                          */
/*    b6...b7: reserved for future use                                        */
/* 4) Flag 2 (reserved for future use)                                        */
/* 5) PPM: ch1/9, lower 8 bits                                                */
/* 6) PPM:                                                                    */
/*    lower nibble: ch1/9, upper 4 bits                                       */
/*    upper nibble: ch2/10, lower 4 bits                                      */
/* 7) PPM: ch2/10, upper 8 bits                                               */
/* 8)...16) PPM of ch3-8/11-16                                                */
/* 17) Extra flag:                                                            */
/*    b0: antenna type, 0=internal, 1=external (iXJT module on FrSky Horus)   */
/*    b1: RX telemetry, 0=ON, 1=OFF                                           */
/*    b2: RX channels, 0=1-8, 1=9-16                                          */
/*    b3...b4: pwr (see radio.h)                                              */
/*    b5: TX S.PORT, 0=ON, 1=OFF                                              */
/*    b6: R9M EU+, 0=NO, 1=YES                                                */
/*    b7: unknown                                                             */
/* 18) CRC low Byte                                                           */
/* 19) CRC high Byte                                                          */
/* 20) Tail (0x7E, no bit stuffing)                                           */
/*                                                                            */
/******************************************************************************/

#include "pxx.h"


#if !defined(RADIO_MODULE_LOCATION)
    #error "Must define RADIO_MODULE_LOCATION in radio_config.h"
#endif
#if !defined(RADIO_MODULE_TYPE)
    #error "Must define RADIO_MODULE_TYPE in radio_config.h"
#endif
#if !defined(RADIO_MODULE_SUBTYPE)
    #error "Must define RADIO_MODULE_SUBTYPE in radio_config.h"
#endif
#if !defined(RADIO_MODULE_VARIANT)
    #error "Must define RADIO_MODULE_VARIANT in radio_config.h"
#endif
#if !defined(RADIO_MODULE_PROTOCOL)
    #error "Must define RADIO_MODULE_PROTOCOL in radio_config.h"
#endif
#if (RADIO_MODULE_PROTOCOL != MODULE_PROTOCOL_PXX)
    #warning "Select PXX as protocol in radio_config.h to use the PXX interface."
#endif
#if !defined(RADIO_MODULE_RF_PROTOCOL)
    #error "Must define RADIO_MODULE_RF_PROTOCOL in radio_config.h"
#endif
#if !defined(RADIO_MODULE_COUNTRY_CODE)
    #error "Must define RADIO_MODULE_COUNTRY_CODE in radio_config.h"
#endif
#if (RADIO_MODULE_LOCATION == MODULE_LOCATION_INTERNAL) && (RADIO_MODULE_TYPE == MODULE_TYPE_XJT)
    #if !defined(RADIO_MODULE_ANTENNA_TYPE)
        #error "Must define RADIO_MODULE_ANTENNA_TYPE in radio_config.h"
    #endif
#else
    #define RADIO_MODULE_ANTENNA_TYPE 0
#endif
#if (RADIO_MODULE_TYPE == MODULE_TYPE_R9M)
    #if !defined(RADIO_MODULE_POWER)
        #error "Must define RADIO_MODULE_COUNTRY_CODE in radio_config.h"
    #endif
#else
    #define RADIO_MODULE_POWER 0
#endif
#if !defined(RADIO_MODULE_BAUDRATE)
    #error "Must define RADIO_MODULE_BAUDRATE in radio_config.h"
#endif
#if !defined(RADIO_CHANNELS)
    #error "Must define RADIO_CHANNELS in radio_config.h"
#endif
#if !defined(RADIO_RECEIVER_ID)
    #error "Must define RADIO_RECEIVER_ID in radio_config.h"
#endif




/******************************************************************************/
/* Private defines                                                            */
/******************************************************************************/

#define PXX_SEND_BIND                       (1 << 0)
#define PXX_SEND_FAILSAFE                   (1 << 4)
#define PXX_SEND_RANGECHECK                 (1 << 5)
#define PXX_SEND_NO_SPORT                   (1 << 5)
#define PXX_SEND_EUPLUS                     (1 << 6)
#define pxxPutByte(x)                       pcmPutByte(x)
#define pxxPutTail                          pcmPutSerialTail
#define FAILSAFE_CHANNEL_HOLD               2000
#define FAILSAFE_CHANNEL_NOPULSE            2001


/******************************************************************************/
/* Private constants                                                          */
/******************************************************************************/

/*
 * This mysterious table is just the CRC of each possible byte. It can be
 * computed using the standard bit-at-a-time methods. The polynomial can
 * be seen in entry 128, 0x8408 (0x1021 reflected).
 * This corresponds to x^0 + x^5 + x^12. Add the implicit x^16, and you have
 * the standard CRC16-CCITT (reflected).
 * The CRC initialization is zero.
 * See www.sunshine2k.de/coding/javascript/crc/crc_js.html.
 */
/*
const uint16_t CRCTable[] = {
  0x0000,0x1189,0x2312,0x329b,0x4624,0x57ad,0x6536,0x74bf,
  0x8c48,0x9dc1,0xaf5a,0xbed3,0xca6c,0xdbe5,0xe97e,0xf8f7,
  0x1081,0x0108,0x3393,0x221a,0x56a5,0x472c,0x75b7,0x643e,
  0x9cc9,0x8d40,0xbfdb,0xae52,0xdaed,0xcb64,0xf9ff,0xe876,
  0x2102,0x308b,0x0210,0x1399,0x6726,0x76af,0x4434,0x55bd,
  0xad4a,0xbcc3,0x8e58,0x9fd1,0xeb6e,0xfae7,0xc87c,0xd9f5,
  0x3183,0x200a,0x1291,0x0318,0x77a7,0x662e,0x54b5,0x453c,
  0xbdcb,0xac42,0x9ed9,0x8f50,0xfbef,0xea66,0xd8fd,0xc974,
  0x4204,0x538d,0x6116,0x709f,0x0420,0x15a9,0x2732,0x36bb,
  0xce4c,0xdfc5,0xed5e,0xfcd7,0x8868,0x99e1,0xab7a,0xbaf3,
  0x5285,0x430c,0x7197,0x601e,0x14a1,0x0528,0x37b3,0x263a,
  0xdecd,0xcf44,0xfddf,0xec56,0x98e9,0x8960,0xbbfb,0xaa72,
  0x6306,0x728f,0x4014,0x519d,0x2522,0x34ab,0x0630,0x17b9,
  0xef4e,0xfec7,0xcc5c,0xddd5,0xa96a,0xb8e3,0x8a78,0x9bf1,
  0x7387,0x620e,0x5095,0x411c,0x35a3,0x242a,0x16b1,0x0738,
  0xffcf,0xee46,0xdcdd,0xcd54,0xb9eb,0xa862,0x9af9,0x8b70,
  0x8408,0x9581,0xa71a,0xb693,0xc22c,0xd3a5,0xe13e,0xf0b7,
  0x0840,0x19c9,0x2b52,0x3adb,0x4e64,0x5fed,0x6d76,0x7cff,
  0x9489,0x8500,0xb79b,0xa612,0xd2ad,0xc324,0xf1bf,0xe036,
  0x18c1,0x0948,0x3bd3,0x2a5a,0x5ee5,0x4f6c,0x7df7,0x6c7e,
  0xa50a,0xb483,0x8618,0x9791,0xe32e,0xf2a7,0xc03c,0xd1b5,
  0x2942,0x38cb,0x0a50,0x1bd9,0x6f66,0x7eef,0x4c74,0x5dfd,
  0xb58b,0xa402,0x9699,0x8710,0xf3af,0xe226,0xd0bd,0xc134,
  0x39c3,0x284a,0x1ad1,0x0b58,0x7fe7,0x6e6e,0x5cf5,0x4d7c,
  0xc60c,0xd785,0xe51e,0xf497,0x8028,0x91a1,0xa33a,0xb2b3,
  0x4a44,0x5bcd,0x6956,0x78df,0x0c60,0x1de9,0x2f72,0x3efb,
  0xd68d,0xc704,0xf59f,0xe416,0x90a9,0x8120,0xb3bb,0xa232,
  0x5ac5,0x4b4c,0x79d7,0x685e,0x1ce1,0x0d68,0x3ff3,0x2e7a,
  0xe70e,0xf687,0xc41c,0xd595,0xa12a,0xb0a3,0x8238,0x93b1,
  0x6b46,0x7acf,0x4854,0x59dd,0x2d62,0x3ceb,0x0e70,0x1ff9,
  0xf78f,0xe606,0xd49d,0xc514,0xb1ab,0xa022,0x92b9,0x8330,
  0x7bc7,0x6a4e,0x58d5,0x495c,0x3de3,0x2c6a,0x1ef1,0x0f78
};
*/

const uint16_t CRCTable_Short[] = {
   0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF,
   0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7
};


/******************************************************************************/
/* Private variables                                                          */
/******************************************************************************/

static uint8_t* pulseDataPtr;
static uint8_t pulseDataBitCount;
static uint8_t pulseDataOneCount;
static uint8_t pulseData[59];
static uint8_t pulseDataByte;
static uint16_t pulseDataCrc;
static uint8_t failsafeCount = 100;
static txmodule module;


/******************************************************************************/
/* Private function definitions                                               */
/******************************************************************************/

// SPI lsb first or msb first ?
// in pxx.cpp: lsb first but bits filled in opposite direction into data reg.

// insert bits msb first, then shift out from SPI TX msb first
static inline void pcmPutSerialBit(uint8_t bit) {
    if (pulseDataBitCount--) {
        pulseDataByte = (pulseDataByte << 1) | bit;
    }
    else {
        *pulseDataPtr++ = pulseDataByte;
        pulseDataByte = bit;
        pulseDataBitCount = 7;  // 7 more bits to be filled in
    }
}

// expand bits to 0 -> 01 (16us), 1 -> 001 (24us)
static void pcmPutSerialPart(uint8_t bit) {
    pcmPutSerialBit(0);
    if (bit) {
        pcmPutSerialBit(0);
    }
    pcmPutSerialBit(1);
}

// insert tail bits as ones
static void pcmPutSerialTail(void) {
    while (pulseDataBitCount) {
        pcmPutSerialBit(1);
    }
    *pulseDataPtr++ = pulseDataByte;
}

// bit stuffing
static void pcmPutBit(uint8_t bit) {
    if (bit) {
        pcmPutSerialPart(1);
        if (++pulseDataOneCount < 5) {
            return;         // don't stuff in a zero
        }
    }
    pcmPutSerialPart(0);
    pulseDataOneCount = 0;
}

static inline uint16_t CRCTable(uint8_t val) {
    return CRCTable_Short[val & 0x0F] ^ (0x1081 * (val >> 4));
}

// Byte translation, msb first, reflected CRC16-CCITT algorithm
static void pcmPutByte(uint8_t byte) {
    pulseDataCrc = (pulseDataCrc << 8) ^ CRCTable((pulseDataCrc >> 8) ^ byte);
    // reversed data:
    // pulseDataCrc = (pulseDataCrc >> 8) ^ CRCTable[(pulseDataCrc ^ byte) & 0xFF];
    // Note: in case of reversed data, low and high Byte in pxxPutCrc must be
    //       inverted
    for (uint8_t i=0; i<8; i++) {
        pcmPutBit(byte & 0x80);
        byte <<= 1;
    }
}

static inline void pxxInitData(void) {
    pulseDataPtr = pulseData;
    pulseDataBitCount = 8;
    pulseDataOneCount = 0;
    pulseDataByte = 0;
}

static inline void pxxInitCrc(void) {
    pulseDataCrc = 0;
}

static inline void pxxPutHead(void) {
    // send 7E, do not CRC nor bit stuff
    pcmPutSerialPart(0);
    pcmPutSerialPart(1);
    pcmPutSerialPart(1);
    pcmPutSerialPart(1);
    pcmPutSerialPart(1);
    pcmPutSerialPart(1);
    pcmPutSerialPart(1);
    pcmPutSerialPart(0);
}

static inline void pxxPutCrcHigh(void) {
    uint8_t byte = pulseDataCrc >> 8;

    for (uint8_t i=0; i<8; i++) {
        pcmPutBit(byte & 0x80);
        byte <<= 1;
    }
}

static inline void pxxPutCrcLow(void) {
    uint8_t byte = pulseDataCrc & 0xFF;

    for (uint8_t i=0; i<8; i++) {
        pcmPutBit(byte & 0x80);
        byte <<= 1;
    }
}

static inline uint16_t limit(uint16_t min, uint16_t val, uint16_t max) {
    if (val < min) {
        return min;
    }
    else if (val > max) {
        return max;
    }
    return val;
}

// prepare PXX pulse train
static void pxxPutBitstream(uint8_t startChannel) {
    uint16_t pulseValue=0, pulseValueLow=0;

    pxxInitCrc();

    // If first bit in bitstream is "0", as it happens in pxxPutHead(),
    // SPI TX seems to pull the line low for two clock cycles. The only
    // way to work around this is by inserting a "1" bit for a start.
    pcmPutSerialBit(1);

    // Preamble ??
    pcmPutSerialPart(1);
    pcmPutSerialPart(1);
    pcmPutSerialPart(1);
    pcmPutSerialPart(1);

    // sync
    pxxPutHead();

    // Rx number
    pxxPutByte(RADIO_RECEIVER_ID);

    // flag 1
    uint8_t flag1 = (module.rfProtocol << 6);
    if (module.flag == MODULE_FLAG_BIND) {
        flag1 |= (module.countryCode << 1) | PXX_SEND_BIND;
    }
    else if (module.flag == MODULE_FLAG_RANGECHECK) {
        flag1 |= PXX_SEND_RANGECHECK;
    }
    else if ((module.failsafeMode != MODULE_FAILSAFE_MODE_NOT_SET) &&
             (module.failsafeMode != MODULE_FAILSAFE_MODE_RECEIVER)) {
        if (failsafeCount-- == 0) {
            failsafeCount = 100;
            flag1 |= PXX_SEND_FAILSAFE;
            module.failsafeMode = MODULE_FAILSAFE_MODE_RECEIVER;    // stop setting failsafe
        }
// significance unclear
#if (RADIO_CHANNELS > 0)
        if (failsafeCount == 0) {
            flag1 |= PXX_SEND_FAILSAFE;
        }
#endif
    }

    pxxPutByte(flag1);

    // flag 2 (no info on bit fields ...)
    pxxPutByte(0);

    // channels 1-8
    for (int i=0; i<8; i++) {
        // failsafe mode?
        if (flag1 & PXX_SEND_FAILSAFE) {
            if (module.failsafeMode == MODULE_FAILSAFE_MODE_HOLD) {
                pulseValue = (i < startChannel ? 4095 : 2047);
            }
            else if (module.failsafeMode == MODULE_FAILSAFE_MODE_NOPULSES) {
                pulseValue = (i < startChannel ? 2048 : 0);
            }
            else {
                if (i < startChannel) {
                    uint32_t failsafeValue = module.ppm.failsafe[8+i];
                    if (failsafeValue == FAILSAFE_CHANNEL_HOLD) {
                        pulseValue = 4095;
                    }
                    else if (failsafeValue == FAILSAFE_CHANNEL_NOPULSE) {
                        pulseValue = 2048;
                    }
                    else {
                        failsafeValue += PPM_CH_CENTER(module, 8+i) - PPM_CENTER;
                        pulseValue = limit(2049, (uint16_t)(failsafeValue * 512 / 682) + 3072, 4094);
                    }
                }
                else {
                    uint32_t failsafeValue = module.ppm.failsafe[i];
                    if (failsafeValue == FAILSAFE_CHANNEL_HOLD) {
                        pulseValue = 2047;
                    }
                    else if (failsafeValue == FAILSAFE_CHANNEL_NOPULSE) {
                        pulseValue = 0;
                    }
                    else {
                        failsafeValue += PPM_CH_CENTER(module, i) - PPM_CENTER;
                        pulseValue = limit(1, (uint16_t)(failsafeValue * 512 / 682) + 1024, 2046);
                    }
                }
            }
        }
        // ... normal, no failsafe mode
        else {
            uint32_t value = module.ppm.channels[i];
            if (i < startChannel) {
                value += PPM_CH_CENTER(module, 8+i) - PPM_CENTER;
                pulseValue = limit(2049, (uint16_t)(value * 512 / 682) + 3072, 4094);
            }
            else {
                value += PPM_CH_CENTER(module, i) - PPM_CENTER;
                pulseValue = limit(1, (uint16_t)(value * 512 / 682) + 1024, 2046);
            }
        }

        if (i & 1) {
            pxxPutByte(pulseValueLow);      // Low byte of even channel
            pxxPutByte((pulseValueLow >> 8) | (pulseValue << 4));  // 4 bits each from 2 channels
            pxxPutByte(pulseValue >> 4);    // High byte of odd channel
        }
        else {
            pulseValueLow = pulseValue;
        }
    }

    uint8_t extra_flags = 0;

    // antenna selection on iXJT Horus/Taranis X-Lite internal module
    // otherwise 0x00
#if (RADIO_MODULE_TYPE == MODULE_TYPE_iXJT)
    extra_flags |= module.antennaType;
#endif

    extra_flags |= (module.pxx.receiver_telem_off << 1);
    extra_flags |= (module.pxx.receiver_channel_9_16 << 2);

    // power settings only for external R9M or R9M Lite module
#if (RADIO_MODULE_LOCATION == MODULE_LOCATION_EXTERNAL) && (RADIO_MODULE_TYPE == MODULE_TYPE_R9M)
    extra_flags |= (module.pxx.power << 3);
#if (RADIO_MODULE_SUBTYPE == MODULE_SUBTYPE_R9M_EUPLUS)
    extra_flags |= PXX_SEND_EUPLUS;
#endif
#endif

    // disable S.PORT if internal module is active
#if (RADIO_MODULE_LOCATION == MODULE_LOCATION_INTERNAL) && (RADIO_MODULE_TYPE == MODULE_TYPE_XJT)
    extra_flags |= PXX_SEND_NO_SPORT;
#endif

    pxxPutByte(extra_flags);

    // CRC
    pxxPutCrcHigh();
    pxxPutCrcLow();

    // sync
    pxxPutHead();
    pxxPutTail();
}

uint8_t PXX_putBitstream(void) {
    pxxInitData();

#if (RADIO_MODULE_BAUDRATE == MODULE_BAUDRATE_HIGH)
    pxxPutBitstream(0);
#if (RADIO_CHANNELS > 8)
    pxxPutBitstream(8);
#endif
#else
#if (RADIO_CHANNELS > 8)
    static uint8_t toggle = 0;
    uint8_t startChannel = 0;
    if (toggle++ & 0x01) {
        startChannel = 8;
    }
    pxxPutBitstream(startChannel);
#else
    pxxPutBitstream(0);
#endif
#endif

    return (uint8_t)(pulseDataPtr - pulseData); // length of data to send
}

void PXX_configureTXModule(void) {
    // permenant settings from radio_config.h
    module.location     = RADIO_MODULE_LOCATION;
    module.type         = RADIO_MODULE_TYPE;
    module.subtype      = RADIO_MODULE_SUBTYPE;
    module.variant      = RADIO_MODULE_VARIANT;
    module.protocol     = RADIO_MODULE_PROTOCOL;
    module.rfProtocol   = RADIO_MODULE_RF_PROTOCOL;
    module.countryCode  = RADIO_MODULE_COUNTRY_CODE;
    module.antennaType  = RADIO_MODULE_ANTENNA_TYPE;

    // initial settings
    module.flag         = MODULE_FLAG_NORMAL_MODE;
    module.failsafeMode = MODULE_FAILSAFE_MODE_NOT_SET;

    // ppm specific settings
    // none so far

    // pxx specific settings
    module.pxx.power    = RADIO_MODULE_POWER;
}

uint8_t* PXX_getBufferPtr(void) {
    return pulseData;
}

void PXX_setBind(bool on) {
    if (on) {
        module.flag |= MODULE_FLAG_BIND;
    }
    else {
        module.flag &= ~MODULE_FLAG_BIND;
    }
}

void PXX_setFailsafeHold(void) {
    module.failsafeMode |= MODULE_FAILSAFE_MODE_HOLD;
}