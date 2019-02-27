/******************************************************************************/
/*                                                                            */
/*  \file       radio.h                                                       */
/*  \date       Sep 2018 -                                                    */
/*  \author     Severin Leven                                                 */
/*                                                                            */
/*  \brief      Definitions for the R/C radio interface                       */
/*                                                                            */
/*  This file is part of the Lasso host library. Lasso is a configurable and  */
/*  efficient mechanism for data transfer between a host (server) and client. */
/*                                                                            */
/*  Inspiration from opentx github project.                                   */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  Target CPU: any 32-bit                                                    */
/*  Ressources: CPU (for com/timer ressources see target-specific module)     */
/*                                                                            */
/******************************************************************************/

#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>
#include <stdbool.h>
#include "radio_config.h"


// inspired by myself
// enum E_MODULE_LOCATION {
#define MODULE_LOCATION_INTERNAL 0
#define MODULE_LOCATION_EXTERNAL 1
// };

// inspired by opentx/radio/src/myeeprom.h
// enum E_MODULE_TYPES {
#define MODULE_TYPE_NONE 0
#define MODULE_TYPE_PPM 1
#define MODULE_TYPE_XJT 2
#define MODULE_TYPE_DSM2 3
#define MODULE_TYPE_CROSSFIRE 4
#define MODULE_TYPE_MULTIMODULE 5
#define MODULE_TYPE_R9M 6
#define MODULE_TYPE_SBUS 7
//};

// inspired by myself
// enum E_MODULE_SUBTYPES {
#define MODULE_SUBTYPE_NONE 0
#define MODULE_SUBTYPE_R9M_FULLSIZE 1
#define MODULE_SUBTYPE_R9M_LITE 2
//};

// inspired by opentx/radio/src/myeeprom.h
enum E_MODULE_VARIANTS {
    MODULE_VARIANT_NONE = 0,
    MODULE_VARIANT_R9M_FCC,
    MODULE_VARIANT_R9M_EU,
    MODULE_VARIANT_R9M_EUPLUS,
    MODULE_VARIANT_R9M_AUPLUS
};

// protocol between MCU and TX module
// inspired by opentx/radio/src/myeeprom.h
//enum E_MODULE_PROTOCOLS {
#define MODULE_PROTOCOL_PPM 0
#define MODULE_PROTOCOL_PPM16 1
#define MODULE_PROTOCOL_PPMSIM 2
#define MODULE_PROTOCOL_PXX 3
#define MODULE_PROTOCOL_DSM2_LP45 4
#define MODULE_PROTOCOL_DSM2_DSM2 5
#define MODULE_PROTOCOL_DSM2_DSMX 6
#define MODULE_PROTOCOL_CROSSFIRE 7
#define MODULE_PROTOCOL_SILV 8
#define MODULE_PROTOCOL_TRAC09 9
#define MODULE_PROTOCOL_PICZ 10
#define MODULE_PROTOCOL_SWIFT 11
#define MODULE_PROTOCOL_MULTIMODULE 12
#define MODULE_PROTOCOL_SBUS 13
//};

// TX module's RF protocols, FrSky radio transmit X/D/L
// refer to: https://agert.eu/blog/index.php?title=FrSky_Telemetry
// inspired by opentx/radio/src/myeeprom.h
enum E_RF_PROTOCOLS {
    MODULE_RF_PROTOCOL_X16 = 0,
    MODULE_RF_PROTOCOL_D8,
    MODULE_RF_PROTOCOL_LR12
};

// inspired by opentx/radio/src/pulses/pulses.h
enum E_MODULE_FLAGS {
    MODULE_FLAG_NORMAL_MODE = 0,
    MODULE_FLAG_RANGECHECK,
    MODULE_FLAG_BIND,
    // MODULE_FLAG_OFF // will need an EEPROM conversion (SLE: ?)
};

// inspired by opentx/radio/src/myeeprom.h
enum E_MODULE_FAILSAFE_MODES {
    MODULE_FAILSAFE_MODE_NOT_SET = 0,
    MODULE_FAILSAFE_MODE_HOLD,
    MODULE_FAILSAFE_MODE_CUSTOM,
    MODULE_FAILSAFE_MODE_NOPULSES,
    MODULE_FAILSAFE_MODE_RECEIVER
};

// inspired by myself
enum E_MODULE_COUNTRY_CODES {
    MODULE_COUNTRY_CODE_US = 0,
    MODULE_COUNTRY_CODE_JP,
    MODULE_COUNTRY_CODE_EU
};

// iXJT antenna types (Horus and Taranis X-Lite)
// inspired by opentx/radio/src/myeeprom.h
#if (RADIO_MODULE_LOCATION == MODULE_LOCATION_INTERNAL) && (RADIO_MODULE_TYPE == MODULE_TYPE_XJT)
enum E_MODULE_ANTENNA_TYPES {
    MODULE_ANTENNA_TYPE_NONE = -1,
    MODULE_ANTENNA_TYPE_INTERNAL,
    MODULE_ANTENNA_TYPE_EXTERNAL
};
#endif


#define IS_MODULE_R9M()                 (module.type == MODULE_TYPE_R9M)
#define IS_MODULE_R9M_FCC()             (IS_MODULE_R9M() && module.variant == MODULE_VARIANT_R9M_FCC)
#define IS_MODULE_R9M_LBT()             (IS_MODULE_R9M() && module.variant == MODULE_VARIANT_R9M_EU)
#define IS_MODULE_R9M_EUPLUS()          (IS_MODULE_R9M() && module.variant == MODULE_VARIANT_R9M_EUPLUS)
#define IS_MODULE_R9M_AUPLUS()          (IS_MODULE_R9M() && module.variant == MODULE_VARIANT_R9M_AUPLUS)
#define IS_MODULE_R9M_FCC_VARIANT()     (IS_MODULE_R9M() && module.variant != MODULE_VARIANT_R9M_EU)


// from opentx/radio/src/pulses/pulses_arm.h
#if (RADIO_MODULE_TYPE == MODULE_TYPE_R9M)
#if (RADIO_MODULE_SUBTYPE >= MODULE_SUBTYPE_R9M_LITE)
// for R9M Lite TX module:
// FCC (868MHz) or EU/LBT (915MHz, listen-before-talk) modes

// FCC, all options for 16ch with telemetry
enum R9M_FCC_PowerSettings {
    MODULE_FCC_POWER_SETTING_100MW_16CH = 0,
    MODULE_FCC_POWER_SETTING_MAX = MODULE_FCC_POWER_SETTING_100MW_16CH
};

// EU/LBT options depend on output power
enum R9M_LBT_PowerSettings {
    MODULE_LBT_POWER_SETTING_25MW_8CH = 0,
    MODULE_LBT_POWER_SETTING_25MW_16CH,
    MODULE_LBT_POWER_SETTING_100MW_16CH_NO_TELEMETRY,
    MODULE_LBT_POWER_SETTING_MAX = MODULE_LBT_POWER_SETTING_100MW_16CH_NO_TELEMETRY
};

#define BIND_TELEMETRY_ALLOWED()    ((!IS_MODULE_R9M_LBT()) || (module.pxx.power < MODULE_LBT_POWER_SETTING_MAX))
#define BIND_CH9_TO_16_ALLOWED()    ((!IS_MODULE_R9M_LBT()) || (module.pxx.power >= MODULE_LBT_POWER_25MW_16CH))

#else
// for R9M fullsize TX module:
// FCC (868MHz) or EU/LBT (915MHz, listen-before-talk) modes

// FCC, all options for 16ch with telemetry
enum R9M_FCC_PowerSettings {
    MODULE_FCC_POWER_SETTING_10MW_16CH = 0,
    MODULE_FCC_POWER_SETTING_100MW_16CH,
    MODULE_FCC_POWER_SETTING_500MW_16CH,
    MODULE_FCC_POWER_SETTING_1W_16CH,
    MODULE_FCC_POWER_SETTING_MAX = MODULE_FCC_POWER_SETTING_1W_16CH
};

// EU/LBT options depend on output power
enum R9M_LBT_PowerSettings {
    MODULE_LBT_POWER_SETTING_25MW_8CH = 0,
    MODULE_LBT_POWER_SETTING_25MW_16CH_NO_TELEMETRY,
    MODULE_LBT_POWER_SETTING_500MW_8CH_NO_TELEMETRY,
    MODULE_LBT_POWER_SETTING_500MW_16CH_NO_TELEMETRY,
    MODULE_LBT_POWER_SETTING_MAX = MODULE_LBT_POWER_SETTING_500MW_16CH_NO_TELEMETRY
};

#define BIND_TELEMETRY_ALLOWED()    ((!IS_MODULE_R9M_LBT()) || (module.pxx.power == MODULE_LBT_POWER_SETTING_25MW_8CH))
#define BIND_CH9_TO_16_ALLOWED()    ((!IS_MODULE_R9M_LBT()) || (module.pxx.power == MODULE_LBT_POWER_SETTING_25MW_16CH_NO_TELEMETRY) || (module.pxx.power == MODULE_LBT_POWER_SETTING_500MW_16CH_NO_TELEMETRY))

#endif
#else

#endif


//enum E_MODULE_BAUDRATES {
#define MODULE_BAUDRATE_LOW 115200
#if (RADIO_MODULE_LOCATION == MODULE_LOCATION_INTERNAL)
#define MODULE_BAUDRATE_HIGH 450000
#else
#define MODULE_BAUDRATE_HIGH 420000
#endif
//};

#if (RADIO_MODULE_PROTOCOL == MODULE_PROTOCOL_PPM)
    #include "ppm.h"
#endif

#if (RADIO_MODULE_PROTOCOL == MODULE_PROTOCOL_PXX)
    #include "ppm.h"
    #include "pxx.h"
#endif

typedef struct S_PPM {
    bool polarity;              // false = idle low, true = idle high
    uint8_t current;
    uint16_t centers[RADIO_CHANNELS];
    uint16_t channels[RADIO_CHANNELS];
    uint16_t failsafe[RADIO_CHANNELS];
} s_ppm;

typedef struct S_PXX {
    uint8_t modelID;            // RX model ID setting
    uint8_t power;              // 0 = 10 mW, 1 = 100 mW, 2 = 500 mW, 3 = 1W
    uint8_t receiver_telem_off; // 0 = receiver telem enabled
    uint8_t receiver_channel_9_16; // 0 = pwm out 1-8, 1 = pwm out 9-16
    bool external_antenna;      // false = internal, true = external antenna
    bool sport_out;             // true = activate TX module S.PORT out
} s_pxx;

typedef struct S_TXMODULE {
    uint8_t location;           // choose from E_MODULE_LOCATIONS enum
    uint8_t type;               // choose from E_MODULE_TYPES enum
    uint8_t subtype;            // choose from E_MODULE_SUBTYPES enum
    uint8_t variant;            // choose from E_MODULE_VARIANTS enum
    uint8_t protocol;           // choose from E_MODULE_PROTOCOLS enum
    uint8_t rfProtocol;         // choose from E_MODULE_RF_PROTOCOLS enum
    uint8_t flag;               // choose_from E_MODULE_FLAGS enum
    uint8_t failsafeMode;       // choose from E_MODULE_FAILSAFE_MODES enum
    uint8_t countryCode;        // choose from E_MODULE_COUNTRY_CODES enum
    uint8_t antennaType;        // choose from E_MODULE_ANTENNA_TYPES enum

    s_ppm ppm;
    s_pxx pxx;
} txmodule;

#endif /* RADIO_H */