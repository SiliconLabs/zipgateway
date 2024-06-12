/* © 2020 Silicon Laboratories Inc. */

/**
 * Contiki events for the test runner process.
 */
enum {
   INIT_DONE, /* The first part of bootup (starting zip_process) is
                 completed.  Now wait for the bootup of the other ZGW
                 processes, eg, for contiki timer to get tcpip up,
                 bridge initialization, etc.

                 Note that test_process will know when tcpip is up,
                 since contiki broadcasts a tcpip_event. */
   RUN_CASE, // start a test case (data pointer is a function pointer to the case init function).
   CASE_DONE, // test case has completed, request the next one.
   INIT_LAN_PACKET_RECEIVED, // zgw initialization: stg arrived on lan, eg from dhcp or mdns.
   SIGUSR1_RECVD, /* Test runner has received a sigusr1 signal, either
                     from a test case or from the ZGW.  The global
                     backup_state and the contents of the backup
                     communication file is used to determine which
                     scenario we are in. */
};

PROCESS_NAME(test_process);
