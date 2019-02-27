/******************************************************************************/
/*                                                                            */
/*  \file       ppm.h                                                         */
/*  \date       Sep 2018 -                                                    */
/*  \author     Severin Leven                                                 */
/*                                                                            */
/*  \brief      Definitions for the PPM protocol of the R/C radio interface   */
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

#ifndef PPM_H
#define PPM_H

#define PPM_MIN                     1000
#define PPM_CENTER                  1500
#define PPM_MAX                     2000
#define PPM_BREAK_US                300
#define PPM_FRAME_US                20000

#if defined(PPM_CENTER_ADJ)         // refer to radio.h
  #define PPM_CH_CENTER(mod, ch)    mod.ppm.centers[ch]
#else
  #define PPM_CH_CENTER(mod, ch)    PPM_CENTER
#endif

#endif /* PPM_H */