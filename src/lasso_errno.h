/******************************************************************************/
/*                                                                            */
/*  \file       lasso_errno.h                                                 */
/*  \date       Jan 2017 - Feb 2019                                           */
/*  \author     Severin Leven                                                 */
/*                                                                            */
/*  \brief      Lasso standard C-header "errno.h" replacement                 */
/*                                                                            */
/*  This file is part of the Lasso host library. Lasso is a configurable and  */
/*  efficient mechanism for data transfer between a host (server) and client. */
/*                                                                            */
/*  Standard header "errno.h" may not be available for target C distribution. */
/*  This file provides replacements of error defines that lasso requires.     */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  Target CPU: any 32-bit                                                    */
/*  Ressources: CPU (for com/timer ressources see target-specific module)     */
/*                                                                            */
/******************************************************************************/

#ifndef LASSO_ERRNO_H
#define LASSO_ERRNO_H

#include <errno.h>

#ifndef EIO
    #define EIO 5
#endif

#ifndef EACCES
    #define EACCES 13
#endif

#ifndef EFAULT
    #define EFAULT 14
#endif

#ifndef EBUSY
    #define EBUSY 16
#endif

#ifndef EINVAL
    #define EINVAL 22
#endif

#ifndef ENODATA
    #define ENODATA 61
#endif

#ifndef EOPNOTSUPP
    #define EOPNOTSUPP 95
#endif

#ifndef ENOTSUP
    #define ENOTSUP 134
#endif

#ifndef EILSEQ
    #define EILSEQ 138
#endif

#ifndef EOVERFLOW
    #define EOVERFLOW 139
#endif

#ifndef ECANCELED
    #define ECANCELED 140
#endif

#endif /* LASSO_ERRNO_H */