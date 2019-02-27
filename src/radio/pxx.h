/******************************************************************************/
/*                                                                            */
/*  \file       pxx.h                                                         */
/*  \date       Sep 2018 -                                                    */
/*  \author     Severin Leven                                                 */
/*                                                                            */
/*  \brief      Definitions for the PXX protocol of the R/C radio interface   */
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

#ifndef PXX_H
#define PXX_H

#include "radio.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t PXX_putBitstream(void);
void PXX_configureTXModule(void);
uint8_t* PXX_getBufferPtr(void);
void PXX_setBind(bool on);
void PXX_setFailsafeHold(void);

#ifdef __cplusplus
}
#endif

#endif /* PXX_H */