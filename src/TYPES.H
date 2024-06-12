/* © 2018 Silicon Laboratories Inc. */
/*********************************  types.h  ********************************
 *           #######
 *           ##  ##
 *           #  ##    ####   #####    #####  ##  ##   #####
 *             ##    ##  ##  ##  ##  ##      ##  ##  ##
 *            ##  #  ######  ##  ##   ####   ##  ##   ####
 *           ##  ##  ##      ##  ##      ##   #####      ##
 *          #######   ####   ##  ##  #####       ##  #####
 *                                           #####
 *          Z-Wave, the wireless language.
 *
 *              Copyright (c) 2007
 *              Zensys A/S
 *              Denmark
 *
 *              All Rights Reserved
 *
 *    This source file is subject to the terms and conditions of the
 *    Zensys Software License Agreement which restricts the manner
 *    in which it may be used.
 *
 *---------------------------------------------------------------------------
 *
 * Description: See below.
 *
 * Author:   Oleg Zadorozhnyy
 *
 * 2007-08-11   OZA     Integrated with Z-Wave environment.
 *
 * Last Changed By:  $Author: oza $
 * Revision:         $Revision: 1.2 $
 * Last Changed:     $Date: 2007/08/19 11:30:20 $
 *
 ****************************************************************************/

/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/

#ifndef _TYPES_H_
#define _TYPES_H_

#define NO_MEM_FUNCTIONS

#include <stdint.h>
#include "pkgconfig.h"

#define auto
#define code
#define CODE
#define xdata


#include <ZW_typedefs.h>


#define XBYTE       BYTE
#define PBYTE  BYTE        /* External data byte residing in lower 256 byte in xdata */
#define IBYTE  BYTE

typedef uint32_t u32;
#define REENTRANT
#define NO_MEM_FUNCTIONS

#define ZW_LOG(a,b)

#include <string.h>
#include <stdbool.h>

typedef	 BYTE	* BYTE_P;

#endif  //_TYPES_H_
