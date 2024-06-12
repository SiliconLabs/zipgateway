/* © 2019 Silicon Laboratories Inc. */

#ifndef _NVM_PROGRAMMER_H_
#define _NVM_PROGRAMMER_H_

/** \ingroup systools
\defgroup zw_programmer ZW Programmer
@{
*/

#define NVM_WRITE_CHUNK_SIZE			64

typedef enum zw_programmer_modes{
	ZW_PROGRAMMER_MODE_UNDEFINED,
	ZW_PROGRAMMER_MODE_NVM_READ,
	ZW_PROGRAMMER_MODE_NVM_WRITE,
	ZW_PROGRAMMER_MODE_FW_UPDATE,
	ZW_PROGRAMMER_MODE_FW_UPDATE_ALREADY_IN_APM,
	ZW_PROGRAMMER_MODE_TEST,
}e_zw_programmer_modes_t;

/**
@}
*/
#endif /* _NVM_PROGRAMMER_H_ */
