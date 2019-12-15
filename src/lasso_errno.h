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
    #define EIO 5           /* I/O error */
#endif

#ifndef EACCES
    #define EACCES 13       /* Permission denied */
#endif

#ifndef EFAULT
    #define EFAULT 14       /* Bad address */
#endif

#ifndef EBUSY
    #define EBUSY 16        /* Mount device busy */
#endif

#ifndef EINVAL
    #define EINVAL 22       /* Invalid argument */
#endif

#ifndef ENOSPC
    #define	ENOSPC 28		/* No space left on device */
#endif

#ifndef ENOMSG
    #define ENOMSG 35       /* No message of desired type */
#endif

#ifndef ENODATA
    #define ENODATA 61      /* No data */
#endif

#ifndef EOPNOTSUPP
    #define EOPNOTSUPP 95   /* Operation not supported */
#endif

#ifndef ENOTSUP
    #define ENOTSUP 134     /* Not supported */
#endif

#ifndef EILSEQ
    #define EILSEQ 138      /* Illegal sequence */
#endif

#ifndef EOVERFLOW
    #define EOVERFLOW 139   /* Value too large for defined data type */
#endif

#ifndef ECANCELED
    #define ECANCELED 140   /* Operation canceled */
#endif

#endif /* LASSO_ERRNO_H */