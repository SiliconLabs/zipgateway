/* © 2020 Silicon Laboratories Inc. */

#ifndef TEST_CONTIKI_H_
#define TEST_CONTIKI_H_

/**
 * \ingroup CTF
 * \defgroup CTF-demo Contiki Test Framework Demo.
 *
 * Simple test case in the contiki test framewor.
 *
 * Demonstrates how to configure and implement a test case for the ZGW
 * that includes contiki.
 *
 * @{
 */

/**
 * Contiki events for the test runner process.
 *
 * Note that test_process can also receive generic contiki events.
 * This includes contiki INIT, broadcast events, timer events, and
 * contiki EXIT.
 *
 * In particular, contiki broadcasts a tcpip_event when
 * tcpip is ready.  This is used in ZGW initialization.
 */
enum {
   /** The first part of bootup (starting zip_process) is completed.
       Now wait for the bootup of the other ZGW processes, eg, for
       contiki timer to get tcpip up, bridge initialization, etc.
   */
   INIT_DONE,

   /** Start a test case (data pointer is a function pointer to the
    * case init function). */
   RUN_CASE,

   /** Test case has completed, request the next one or request termination. */
   CASE_DONE,

   /** zgw initialization: stg arrived on lan, eg from dhcp or mdns. */
   INIT_LAN_PACKET_RECEIVED,

   /** Test runner has received a sigusr1 signal, either from a test
       case or from the ZGW.  The global backup_state and the contents
       of the backup communication file is used to determine which
       scenario we are in.  See the zgw_state test for an example of
       how to test with signals.

       \note Signals cannot be fully tested in the ctest runner
       framework.  */
   SIGUSR1_RECVD,
};

PROCESS_NAME(test_process);
/** @} */
#endif
