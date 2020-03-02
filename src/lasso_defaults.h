/******************************************************************************/
/*                                                                            */
/*  \file       lasso_defaults.h                                              */
/*  \date       Feb 2020 - Feb 2020                                           */
/*  \author     Severin Leven                                                 */
/*                                                                            */
/*  \brief      Lasso default configuration and user configuration checks.    */
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

#ifndef LASSO_DEFAULTS_H
#define LASSO_DEFAULTS_H

// General availability and validity checks
#ifndef LASSO_HOST_COMMAND_ENCODING
    #define LASSO_HOST_COMMAND_ENCODING LASSO_ENCODING_RN
#endif

#ifndef LASSO_HOST_STROBE_ENCODING
    #define LASSO_HOST_STROBE_ENCODING LASSO_ENCODING_NONE
#endif

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

#ifndef LASSO_HOST_NOTIFICATIONS
    #define LASSO_HOST_NOTIFICATIONS (0)
#endif

#if (LASSO_HOST_NOTIFICATIONS == 1)
    #if (LASSO_HOST_STROBE_ENCODING == LASSO_ENCODING_NONE)
        #error Notifications can only be used in full COBS/ESCS encoding mode
    #endif
    #if (LASSO_HOST_NOTIFICATION_BUFFER_SIZE < 2)
        #error Notifications buffer must be able to hold more than 2 Bytes
    #endif
#endif

#ifndef LASSO_HOST_NOTIFICATION_USE_PRINTF
    #define LASSO_HOST_NOTIFICATION_USE_PRINTF (0)
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

// Lasso host outgoing (notification) buffer size (default, unlimited)
#if (LASSO_HOST_NOTIFICATIONS == 1)
    #ifndef LASSO_HOST_NOTIFICATION_BUFFER_SIZE
        #define LASSO_HOST_NOTIFICATION_BUFFER_SIZE     (256)
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

#endif /* LASSO_DEFAULTS_H */