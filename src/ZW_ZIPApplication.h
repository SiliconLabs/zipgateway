/* © 2014 Silicon Laboratories Inc.
 */

#ifndef ZW_ZIPAPPLICATION_H_
#define ZW_ZIPAPPLICATION_H_

#include <TYPES.H>
#include <ZW_typedefs.h>
#include "ZW_basis_api.h"
#include "ZW_classcmd_ex.h"
#include "ZW_SendDataAppl.h"
#include "ZW_udp_server.h"
extern BYTE IPNIF[];
extern BYTE IPNIFLen;
extern BYTE IPSecureClasses[];
extern BYTE IPnSecureClasses;

extern BYTE MyNIF[];
extern BYTE MyNIFLen;
extern BYTE SecureClasses[];
extern BYTE nSecureClasses;

extern BYTE nSecureClassesPAN;
extern BYTE SecureClassesPAN[];

/**
 * The highest security scheme supported by this node.
 */
extern security_scheme_t net_scheme;

void ApplicationDefaultSet();
void ApplicationInitProtocols(void);

/**
 * Call if the one of the NIFs has been dynamically updated.
 */
void CommandClassesUpdated() ;
/** Add more command classes to NIF and update Z-Wave target.
 * Make sure to add less than sizeof(MyNIF) CCs in total. */
void AddUnsocDestCCsToGW(BYTE *ccList, BYTE ccCount);
void AddSecureUnsocDestCCsToGW(BYTE *ccList, BYTE ccCount);

/**
 * Set should_send_nodelist flag
 */
void set_should_send_nodelist();

/**
 * Send a nodelist to the unsolicited destination, if
 * \ref should_send_nodelist is true.
 */
void send_nodelist();

/**
 * Called when SerialAPI restarts (typically when power cycled)
 *
 * pData contains the following data:
 *   ZW->HOST: bWakeupReason | bWatchdogStarted | deviceOptionMask |
 *          nodeType_generic | nodeType_specific | cmdClassLength | cmdClass[]
 */
void SerialAPIStarted(uint8_t *pData, uint8_t length);

/**
 * Set the pre-inclusion NIF, which is an non-seucre nif.
 * \param target_scheme The scheme we wish to present
 */
void SetPreInclusionNIF(security_scheme_t target_scheme);

/**
 * Stop the timer which probes for new nodes.
 */
void StopNewNodeProbeTimer();


/**
 * Set up our nif and capabilities and init security layer.
 *
 * Reads the serial device's capabilities and configures those of the
 * zipgateway's capabilities that depend on this: primarily the NIFs,
 * but also smart start capability, \ref controller_role, and
 * security.
 */
BYTE ApplicationInitNIF(void);

/**
 * Translate a numeric security scheme to a string name.
*/
const char* network_scheme_name(security_scheme_t scheme);

/**
 * The generic application command handler for Z/IP frames.
 * \param c The connection describing the source and destination of the message
 * \param pData pointer the Z-Wave command to be handled
 * \param bDatalen length of the Z-Wave command to be handled
 * \return TRUE is the frame has be processed false if the frame prossing has
 * been delayed.
 */
extern bool ApplicationIpCommandHandler(zwave_connection_t *c, void *pData, u16_t bDatalen);


#endif /* ZW_ZIPAPPLICATION_H_ */
