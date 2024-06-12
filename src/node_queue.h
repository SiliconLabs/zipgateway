/* © 2014 Silicon Laboratories Inc. */

#ifndef NODE_QUEUE_H_
#define NODE_QUEUE_H_
/**
 * \defgroup node_queue Node Queue
 *
 * The Node Queue component queues up frames for the Z-Wave network and controls access to the Z-Wave interface.
 *
 * The Z/IP Gateway needs the component to
 * fulfill the following requirements:
 *
 * - To prevent one defective node from completely stalling
 * communication to other nodes, the Z/IP Router MUST abort the
 * transmission after only a few retransmissions.
 *
 * - Aborted frame transmissions MUST be resumed after the successful
 * delivery of all other frames.
 *
 * This is needed for the following reasons:
 *
 * Several LAN IP applications may be communicating to several nodes via
 * the Z/IP Router in a concurrent manner. Other use cases include a number
 *  of LAN IP applications sending packets to the same node or one LAN IP
 *  application sending out a burst of packets for different destinations.
 *
 * All arriving packets must be queued by the Z/IP Router to provide
 * a fair delivery sequence and to avoid frame reordering.
 *
 * After forwarding a packet, the Z/IP Router may time out waiting for the
 * delivery acknowledgment resulting in a number of retransmissions for
 * that same destination.
 *
 * \note The node may have been turned off; causing the protocol layer to start
 * looking for it everywhere. This may take several seconds. In the meantime, all
 * other communication to and from the Z/IP Router will be stalled.
 * The Z-Wave API was not designed for concurrent packet streams and advanced
 * TX re-scheduling as this would require memory resources not available in small
 *  memory footprint systems typically found in Z-Wave controllers.
 *
 *  The implementation of the delivery mechanism must fulfill the following requirements:
 * - First-attempt transmission timeout:
 *   If the SendData API call has not returned after 500 msec (@ 40 kbit/s), SendDataAbort
 *   MUST be called, allowing the next frame in the line to be served.
 *   The Z/IP application MUST wait for a call-back event before issuing another
 *   SendData call.
 *
 * - No frame reordering:
 *   If a transmission is aborted, all frames for that destination MUST be postponed.
 *   When transmission is resumed, the first frame transmitted for that destination MUST
 *   be the one that was aborted previously.
 *
 * - Resumed transmission timeout:
 *   After all first-attempt transmissions have been completed or aborted, the Z/IP
 *   Router MUST resume transmissions for apparently failing nodes.
 *
 *@{
 */


/**
 * Initialize the node queue.
 */
void node_queue_init();

/**
 * Call when packet in uip_buf is for a Z-Wave node.
 */
int node_input_queued(nodeid_t node, int decrypted);

/**
 * Must be called when when queues has been updated.
 */
void process_node_queues();

enum en_queue_state
{
  QS_IDLE, QS_SENDING_FIRST, QS_SENDING_LONG
};

/**
 */
enum en_queue_state get_queue_state();

/** Check that the node queues are idle.
 *
 * To be idle, node queue state must be idle and both queues empty.
 */
bool node_queue_idle(void);

/**
 * @}
 */
#endif /* NODE_QUEUE_H_ */
