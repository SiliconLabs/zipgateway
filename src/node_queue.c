/* © 2014 Silicon Laboratories Inc. */

#include "contiki-net.h"
#include "TYPES.H"
#include "ZW_classcmd.h"
#include "ZW_classcmd_ex.h"
#include "ClassicZIPNode.h"
#include "crc16.h"
#include "zip_router_ipv6_utils.h" /* nodeOfIP */
#include "zip_router_config.h"
#include "router_events.h"
#include "Security_Scheme0.h"
#include "ResourceDirectory.h"
#include "Mailbox.h"
#include "CC_NetworkManagement.h"

#include "uip-debug.h"
#include "node_queue.h"
#include "crc32alg.h"
#include "ZIP_Router_logging.h"
#include "DTLS_server.h"
#define UIP_IP_BUF                          ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
struct uip_packetqueue_handle first_attempt_queue;
struct uip_packetqueue_handle long_queue;
static struct ctimer queue_timer;



#define BLACKLIST_LENGTH 2
static uint32_t black_list_crc32[BLACKLIST_LENGTH];

static enum en_queue_state queue_state;

enum en_queue_state
get_queue_state()
{
  return queue_state;
}

bool node_queue_idle(void) {
   return ((get_queue_state() == QS_IDLE)
           && (uip_packetqueue_len(&first_attempt_queue) == 0)
           && (uip_packetqueue_len(&long_queue) == 0));
}

/**
 * Add fingerprint of uipbuf to blacklist
 */
static void
add_fingerprint_uip_buf()
{
  static uint8_t n = 0;

  black_list_crc32[n] = crc32(&uip_buf[UIP_LLH_LEN], uip_len, 0xFFFF);
  n++;
  if (n >= BLACKLIST_LENGTH)
    n = 0;
}

/**
 * Check if fingerprint of uip_buf is in blacklist
 */
static uint8_t
check_fingerprint_uip_buf()
{
  uint16_t crc;
  uint8_t i;

  crc = crc32(&uip_buf[UIP_LLH_LEN], uip_len, 0xFFFF);
  for (i = 0; i < BLACKLIST_LENGTH; i++)
  {
    if (crc == black_list_crc32[i])
      return TRUE;
  }
  return FALSE;
}

//#define TO4085_FIX
#ifndef TO4085_FIX
/**
 * Check long queue to see if there already is frame queued for this node.
 * @return true if a queued frame exists.
 */
static struct uip_packetqueue_packet*
queued_elements_existes(nodeid_t node)
{
  struct uip_packetqueue_packet* p;
  struct uip_ip_hdr* ip;
  for (p = (struct uip_packetqueue_packet*) list_head((list_t) &(long_queue.list)); p; p =
      (struct uip_packetqueue_packet*) list_item_next(p))
  {
    ip = (struct uip_ip_hdr*) p->queue_buf;
    if (nodeOfIP(&(ip->destipaddr)) == node)
    {
      return p;
    }
  }
  return 0;
}
#endif /* TO4085_FIX */

#define UIP_UDP_BUF                        ((struct uip_udp_hdr *)&uip_buf[uip_l2_l3_hdr_len])

void handle_keep_alive(uint8_t ack_req_byte)
{
    struct uip_udp_conn* c = get_udp_conn();
    int node = nodeOfIP(&UIP_IP_BUF->destipaddr);
    const u8_t keep_alive_ack[] = {COMMAND_CLASS_ZIP,COMMAND_ZIP_KEEP_ALIVE,ZIP_KEEP_ALIVE_ACK_RESPONSE};

    DBG_PRINTF("Sending keep alive ACK from z-wave node %d to port:%d of IP:", node, UIP_HTONS(c->rport));
    uip_debug_ipaddr_print(&c->ripaddr);

    if( ack_req_byte & ZIP_KEEP_ALIVE_ACK_REQUEST) {
      udp_send_wrap(c, &keep_alive_ack, sizeof(keep_alive_ack), 0, 0);
    }
}
/**
 * Called when a new frame is to be transmitted to the PAN
 */
int
node_input_queued(nodeid_t node, int decrypted)
{
  u8_t rc;

  //DBG_PRINTF("node_input_queued: %d\n", node);
#ifndef DISABLE_DTLS
  if((UIP_UDP_BUF ->destport == UIP_HTONS(DTLS_PORT)) && (!decrypted))
  {
    uip_appdata = &uip_buf[UIP_LLH_LEN + UIP_IPUDPH_LEN];
    uip_len -= UIP_IPUDPH_LEN;
    DBG_PRINTF("node_input_queued: Packet for DTLS_PORT uip_len: %d from:", uip_len);
    uip_debug_ipaddr_print(&UIP_IP_BUF->srcipaddr);
    /* Line below calls UDPCommandHandler() if for for gw or ClassicZIPNode_dec_input() for z-wave nodes*/
    process_post_synch(&dtls_server_process,DTLS_SERVER_INPUT_EVENT,0);
    return TRUE;
  } else if ((UIP_UDP_BUF->destport == UIP_HTONS(DTLS_PORT)) || (UIP_UDP_BUF->destport == UIP_HTONS(ZWAVE_PORT))) {
#else
  if (UIP_UDP_BUF->destport == UIP_HTONS(ZWAVE_PORT)) { 
#endif
     uint8_t *cc = &uip_buf[UIP_LLH_LEN + UIP_IPUDPH_LEN];
     if ((cc[0] == COMMAND_CLASS_ZIP) && (cc[1] == COMMAND_ZIP_KEEP_ALIVE)) { 
         handle_keep_alive(cc[2]);
         return TRUE;
     }
  }

  if (check_fingerprint_uip_buf())
  {
#ifndef __ASIX_C51__
    WRN_PRINTF("Dropping duplicate UDP frame.\n");
#endif
    return TRUE;
  }
  else
  {
    /* We don't want to queue this frame again so generate a blacklist finger print. */
    add_fingerprint_uip_buf();
  }

  /* Disable first_attempt_queue due to TO4085.
   * Long term fix is TBD, perhaps go directly to long_queue
   * when destination is FLiRS node. */
#ifndef TO4085_FIX
  ASSERT(uip_len > UIP_IPH_LEN);
  /**
   * If this packet will never go to the mailbox, consider if it
   * should go directly to the long queue:
   * - If there are already packages for the node in the long queue go directly to the long queue
   * - If the node is FLIRS, go directly to long queue.
   * - If there is nothing queued at all.  This is to avoid two S2
   *   encryptions of the same frame within a short timespan - frames
   *   that go to the mailbox do not go to long q, so they will only
   *   have one encryption.
   */
  ASSERT(uip_len > UIP_IPH_LEN);
  rd_node_mode_t node_rd_mode = rd_get_node_mode(node);
  uint8_t use_long_q = FALSE;
  if ((cfg.mb_conf_mode == DISABLE_MAILBOX)
      || (node_rd_mode != MODE_MAILBOX)) {
    if (queued_elements_existes(node)
        || ((node_rd_mode & 0xFF) == MODE_FREQUENTLYLISTENING)
        || (((uip_packetqueue_len(&first_attempt_queue)) == 0) &&
            ((uip_packetqueue_len(&long_queue)) == 0))) {
        use_long_q = TRUE;
      }
  }
  if (use_long_q)
  {
    rc = uip_packetqueue_alloc(&long_queue, &uip_buf[UIP_LLH_LEN], uip_len, 60000) != 0;
  }
  else
#endif
  { // put it in first queue. The packet is encrypted
    rc = uip_packetqueue_alloc(&first_attempt_queue, &uip_buf[UIP_LLH_LEN], uip_len, 60000) != 0;
  }

  /* call process_node_queues() */
  process_post(&zip_process, ZIP_EVENT_QUEUE_UPDATED, 0);
  return rc;
}

/**
 * Timeout for first attempt
 */
static void
queue_send_timeout(void* user)
{
  ClassicZIPNode_AbortSending();
}

/**
 * Test if frame is a Wake Up No More Information.
 */
int is_wu_no_more_info_frame(uint8_t* pkt)
{
  ZW_APPLICATION_TX_BUFFER *pCmd;
  ZW_COMMAND_ZIP_PACKET* zip_pkt;
  uint8_t ext_hdr_len = 0;

  if (is_zip_pkt(pkt)) {
      zip_pkt = (ZW_COMMAND_ZIP_PACKET*)(pkt + UIP_IPUDPH_LEN);
      /* if there is extension header */
      if (zip_pkt->flags1 & ZIP_PACKET_FLAGS1_HDR_EXT_INCL) {
          ext_hdr_len = (uint8_t)zip_pkt->payload[0];
      }
      pCmd = (ZW_APPLICATION_TX_BUFFER *)(&zip_pkt->payload[ext_hdr_len]);

      if (((int )pCmd->ZW_Common.cmdClass == COMMAND_CLASS_WAKE_UP) &&
          ((int )pCmd->ZW_Common.cmd == WAKE_UP_NO_MORE_INFORMATION)) {
          return TRUE;
      }
  }
  return FALSE;
}

/* These are hack functions to remember if the packet has been
 * re-queued.
 *
 * Re-queueing happens when the ClassicZIPNode module fails to
 * transmit the frame.  Depending on the reason for the fail (eg,
 * another frame is currently being transmitted, the Gateway is
 * currently busy, or the node is not responding), ClassicZIPNode
 * calls the queue_send_done() callback with different status codes.
 *
 * The "queued" flag is set on a re-queued frame in two of these
 * scenarios:
 * - when re-queueing from first-q to long-q after
 *   TRANSMIT_COMPLETE_NO_ACK or
 * - when re-queueing from any queue to long queue after
 *   TRANSMIT_COMPLETE_REQUEUE_QUEUED.
 *
 * We have the following invariants about the re-q flag:
 * 1) A frame in the short queue never has the re-q flag set.
 * 2) A frame in the long queue always has the re-q flag set, except
 *    - if its destination is a FLIRS node,
 *    - if both queues were empty when it was queued up, or
 *    - if there were frames for the same destination in
 *      the long queue when the frame was queued up.
 *    In these cases the re-q flag is only set after a send-attempt
 *    has failed.
 * 3) A frame in the long queue is never for a mailbox node, except
 *    if it is WUNMI or
 *    if it was requeued by ClassicZIPNode because NM, RD or Mailbox were busy.

 * These functions change uip_buf IP header as we dont use it */

/** Check the "already requeued" re-q flag.
 */
uint8_t isPacketRequeued(BYTE *buf)
{
    return (buf[0] & 1);
}

/** Set the "already requeued" re-q flag.
 *
 * This is done when a frame is moved from the first-attempt queue to
 * the long queue because
 * 1) \ref queue_send_done() is called with \ref TRANSMIT_COMPLETE_NO_ACK
 *    on a frame that is then moved from first attempt queue to long
 *    queue.
 * 2) \ref queue_send_done() is called with \ref TRANSMIT_COMPLETE_REQUEUE_QUEUED.
 */
void setPacketRequeued(BYTE *buf)
{
    buf[0] |= 1;
}

/** Handle mailbox filtering for a Node Queue frame.  Return status.
 * 
 * Before the Node Queue moves frames to the long queue, frames for a
 * sleeping node should be filtered out and moved to the Mailbox:
 * - If the frame is for a mailbox node, move it to \ref mailbox and return TRUE.
 * - If the frame is not for a mailbox node, do nothing to the frame and return FALSE.
 *
 * \note WUNMI frames are not moved to Mailbox.
 * 
 * This function does not remove the frame from its current node
 * queue, that must be handled by caller.
 *
 * \param node The destination node of the frame
 * \param node_rd_mode Mode of the destination node (\ref rd_node_mode_t).
 * \param sent_buffer Pointer to the frame
 * \param send_len Size of the frame
 * \return Whether the frame was moved.
 */
static int queue_move_to_mailbox(nodeid_t node, rd_node_mode_t node_rd_mode,
                                 BYTE* sent_buffer, uint16_t send_len) {
   int frame_handled = FALSE;

   if ((cfg.mb_conf_mode != DISABLE_MAILBOX) && (node_rd_mode == MODE_MAILBOX) ) {
      if (is_wu_no_more_info_frame(sent_buffer)) {
         DBG_PRINTF("Wakeup no more info frame sent from ZIP Client to node: %d is NOT put in Mailbox\n",
                    node);
      } else {
         memcpy(uip_buf + UIP_LLH_LEN, sent_buffer, send_len);
         uip_len = send_len;
         mb_post_uipbuf(0, 0);
         uip_len = 0;
         frame_handled = TRUE;
      }
   }
   if (node_rd_mode == MODE_FIRMWARE_UPGRADE) {
       DBG_PRINTF("Node %d is sleeping node but in firmware upgrade. Not putting frame to Mailbox\n", node);
   }
   return frame_handled;
}

/**
 * Send done event handler.
 *
 * This is the callback function sent to ClassicZIPNode_input() when
 * transmitting a frame.
 *
 * If status IS TRANSMIT_COMPLETE_OK, the frame can be removed from
 * the queue.
 *
 * If status is TRANSMIT_COMPLETE_REQUEUE, ClassicZIPNode_input()
 * itself is busy sending another frame.  The node_queue goes back to
 * idle and keeps the frame in its original queue. This does not set
 * the re-q flag.
 *
 * In three cases, status can be TRANSMIT_COMPLETE_REQUEUE_QUEUED:
 * - if Network Management is busy (except probe_by_SIS)
 * - if the probe engine is busy
 * - if Mailbox is busy and fromMailbox (or the re-q flag) is NOT set on the call.
 * I.e., Node Queue should not send because another Gateway module has priority.
 *
 * When queue_send_done() receives a TRANSMIT_COMPLETE_REQUEUE_QUEUED,
 * a frame in the first attempt queue is moved to the long queue or
 * the mailbox (if it is for a mailbox node), and a frame in the long
 * queue stays there.  If the frame is moved to the long queue, the
 * re-q flag is set on the frame (except if it is for a FLIRS node).
 *
 * Note that if a frame has already been re-queued once,
 * it will still be sent, even if the Mailbox is busy (not in the
 * other cases).
 *
 * When status is TRANSMIT_COMPLETE_NO_ACK, a frame from the first
 * attempt queue is moved to the mailbox if the frame is for a mailbox
 * node.  Otherwise, it is moved to the long queue (and the re-q flag
 * is set).
 */
static void queue_send_done(BYTE status, BYTE* sent_buffer, uint16_t send_len)
{
  u8_t* data;

  ctimer_stop(&queue_timer);

  if (queue_state == QS_SENDING_FIRST) {
      data = uip_packetqueue_buf(&first_attempt_queue);
  } else if  (queue_state == QS_SENDING_LONG) {
      data = uip_packetqueue_buf(&long_queue);
  } else {
      return;
  }

  if(data == 0) {
    assert(0);
    queue_state = QS_IDLE;
    return;
  }

  struct uip_ip_hdr* iph = (struct uip_ip_hdr*) data;
  nodeid_t node = nodeOfIP(&iph->destipaddr);
  rd_node_mode_t node_rd_mode = rd_get_node_mode(node);

  LOG_PRINTF("queue_send_done to node %i queue %i status: %s\n",
             node, queue_state, transmit_status_name(status));

  if(status == TRANSMIT_COMPLETE_OK) {
    mb_node_transmission_ok_event(node);
  }

  if (status == TRANSMIT_COMPLETE_REQUEUE) {
     queue_state = QS_IDLE;
     return;
  }

  if (status == TRANSMIT_COMPLETE_REQUEUE_QUEUED) {
    /* This happens if NetworkManagement or ResourceDirectory are busy
     * or if (Mailbox is sending and this frame does not have mailbox
     * or re-q flag set). */
    /* Frames for sleeping nodes must be queued in mailbox.  All other
     * frames are sent to long queue. */
    if (!queue_move_to_mailbox(node, node_rd_mode, sent_buffer, send_len)) {
       if (((node_rd_mode & 0xFF) != MODE_FREQUENTLYLISTENING)) {
          /* Set bit to force frame through Mailbox flushing the next
             time around. */
          /* Frames for FLIRS nodes should not be allowed to interrupt
             Mailbox. */
          setPacketRequeued(sent_buffer);
       }
       uip_packetqueue_alloc(&long_queue, sent_buffer, send_len, 60000);
    }
    /* Take the frame out of first_attempt_queue if q-state is
     * QS_SENDING_FIRST, otherwise take it out of long_queue. */
    uip_packetqueue_pop( queue_state == QS_SENDING_FIRST ?  &first_attempt_queue : &long_queue);
    queue_state = QS_IDLE;
    return;
  }

  if (queue_state == QS_SENDING_FIRST)
  {
    /* if the packet is not ack'd then we put it in mailbox or long queue*/
    if ((status == TRANSMIT_COMPLETE_NO_ACK) && send_len && sent_buffer)
    {
      /* Frames for sleeping nodes must be queued in mailbox, except
         WUNMI.  All other frames are moved to long queue. */
       if (!queue_move_to_mailbox(node, node_rd_mode, sent_buffer, send_len)) {
          setPacketRequeued(sent_buffer);
          uip_packetqueue_alloc(&long_queue, sent_buffer, send_len, 60000);
       }
    }
    uip_packetqueue_pop(&first_attempt_queue); //remove from first queue
  }
  else if (queue_state == QS_SENDING_LONG)
  {
    uip_packetqueue_pop(&long_queue); // no matter if it was transmitted or not from long queue we pop it
  }

  queue_state = QS_IDLE;
  process_post(&zip_process, ZIP_EVENT_QUEUE_UPDATED, 0);
}

/**
 * Must be called when when queues has been updated
 */
void
process_node_queues()
{
  nodeid_t node;
  BOOL already_requeued;
  struct uip_packetqueue_handle *q = 0;

  if (queue_state == QS_IDLE)
  {
    if (uip_packetqueue_len(&first_attempt_queue))
    {
      q = &first_attempt_queue;
      queue_state = QS_SENDING_FIRST;
      ClassicZIPNode_setTXOptions(
      TRANSMIT_OPTION_ACK);
      ClassicZIPNode_sendNACK(FALSE);
      /* TODO-KM: Why is the timeout 800ms here?? */
      ctimer_set(&queue_timer, 800, queue_send_timeout, q);
      DBG_PRINTF("Sending first attempt\n");
    }
    else if (uip_packetqueue_len(&long_queue))
    {
      q = &long_queue;
      queue_state = QS_SENDING_LONG;
      ClassicZIPNode_setTXOptions(
      TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_AUTO_ROUTE | TRANSMIT_OPTION_EXPLORE);
      ClassicZIPNode_sendNACK(TRUE);
      DBG_PRINTF("Sending long attempt\n");
    }
  }

  if (q && queue_state != QS_IDLE)
  {
    uip_len = uip_packetqueue_buflen(q);
    DBG_PRINTF("process_node_queues: uip_len:%d\n", uip_len);
    if (uip_len)
    {
      memcpy((void*) &uip_buf[UIP_LLH_LEN], (void*) uip_packetqueue_buf(q), uip_len);
      node = nodeOfIP(&(UIP_IP_BUF->destipaddr));
      ASSERT(node);
      DBG_PRINTF("destination: %d\n", node);

      /* Extend probe timer if we are doing smart start inclusion
       * and are forwarded a frame to the newly included node
       * No frames sent to node for 10 seconds means that middleware has finished probing
       */
      if (node == NM_get_newly_included_nodeid())
      {
        extend_middleware_probe_timeout();
      }

      already_requeued = isPacketRequeued(uip_packetqueue_buf(q));

      if (!ClassicZIPNode_input(node, queue_send_done, FALSE, already_requeued))
      {
        queue_send_done(TRANSMIT_COMPLETE_FAIL, 0, 0);
      }
      uip_len = 0;
    }
    else
    {
      ASSERT(0);
      queue_state = QS_IDLE;
      uip_packetqueue_pop(q);
    }
  } else {
     /* Notify that node queues are idle */
     process_post(&zip_process, ZIP_EVENT_COMPONENT_DONE, 0);
  }
}

void
node_queue_init()
{
  /* Free up already initialized queues */
  if (queue_state != QS_IDLE)
  {
    while (uip_packetqueue_len(&first_attempt_queue))
    {
      uip_packetqueue_pop(&first_attempt_queue);
    }
    while (uip_packetqueue_len(&long_queue))
    {
      uip_packetqueue_pop(&long_queue);
    }
  }
  uip_packetqueue_new(&first_attempt_queue);
  uip_packetqueue_new(&long_queue);
  queue_state = QS_IDLE;
}
