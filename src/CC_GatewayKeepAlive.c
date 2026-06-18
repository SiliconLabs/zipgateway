/* CC_GatewayKeepAlive.c - Z/IP Gateway Keep-Alive / Important Node / App State module
 *
 * Handles:
 *   GATEWAY_IMPORTANT_NODE_LIST_SET  (0x13): store list, reply REPORT (0x14)
 *   GATEWAY_APP_STATE_SET            (0x15): store state, reply REPORT (0x16)
 *   GATEWAY_WAKE_NOTIFY_REPORT       (0x17): unsolicited to host
 */

#include "CC_GatewayKeepAlive.h"
#include "ClassicZIPNode.h"
#include "S2_wrap.h"
#include "Serialapi.h"
#include "ZIP_Router_logging.h"
#include "zip_router_config.h"
#include <string.h>

static uint16_t                   max_important_devices = GW_IMPORTANT_NODE_MAX_ENTRIES;
static uint8_t                    max_important_frame_bytes = GW_IMPORTANT_LIST_FRAME_BYTES;
static gw_important_node_entry_t  important_node_list[GW_IMPORTANT_NODE_MAX_ENTRIES];
static uint16_t                   important_node_count = 0;
static uint8_t                    app_state = GW_APP_STATE_AWAKE;

void gw_keepalive_init(void)
{
    uint8_t max_dev = 0;
    uint8_t max_frame = 0;
    int sc;

    memset(important_node_list, 0, sizeof(important_node_list));
    important_node_count = 0;
    app_state = GW_APP_STATE_AWAKE;

    sc = SerialAPI_GetModuleCapabilities(&max_dev, &max_frame);
    if (sc == SERIALAPI_HOST_HIBERNATION_OK)
    {
        max_important_devices = max_dev;
        max_important_frame_bytes = max_frame;
        LOG_PRINTF("GW KeepAlive: host hibernation module caps - max important devices=%u, max list frame len=%u\n",
                   (unsigned)max_important_devices, (unsigned)max_important_frame_bytes); // NOSONAR
    }
    else
    {
        WRN_PRINTF("GW KeepAlive: SerialAPI_GetModuleCapabilities failed, return %d\n", sc); // NOSONAR
        LOG_PRINTF("GW KeepAlive: using default max important-device count=%u, list frame length=%u\n",
                   (unsigned)max_important_devices, (unsigned)max_important_frame_bytes); // NOSONAR
    }

    LOG_PRINTF("GW KeepAlive: module initialized\n"); // NOSONAR
}

/*
 * Parse GATEWAY_IMPORTANT_NODE_LIST_SET payload (after cmdClass + cmd bytes):
 *   [0..1] count (uint16, LSB first)
 *   [2..]  count × { node_id(2), keep_alive_win_min(2) }   all LSB first
 */
int gw_keepalive_handle_important_node_set(const uint8_t *payload, uint16_t len)
{
    uint16_t count, i;

    if (len < 2)
    {
        return GW_KEEPALIVE_ERR_INVALID_PAYLOAD;
    }

    count = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);

    if (count > max_important_devices)
    {
        return GW_KEEPALIVE_ERR_INVALID_PAYLOAD;
    }

    if (len < (uint16_t)(2 + count * 4))
    {
        return GW_KEEPALIVE_ERR_INVALID_PAYLOAD;
    }

    important_node_count = count;
    for (i = 0; i < count; i++)
    {
        const uint8_t *p = payload + 2 + i * 4;
        important_node_list[i].node_id            = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        important_node_list[i].keep_alive_win_min = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
    }

    LOG_PRINTF("GW KeepAlive: GATEWAY_IMPORTANT_NODE_LIST_SET: %u entries\n", (unsigned)count); // NOSONAR
    for (i = 0; i < count; i++)
    {
        DBG_PRINTF("node_id=%u keep_alive_win=%u min\n",
                   (unsigned)important_node_list[i].node_id,
                   (unsigned)important_node_list[i].keep_alive_win_min); // NOSONAR
    }

    /* Forward to NCP via SerialAPI (use important_node_list directly) */
    if (count == 0)
    {
        int sc = SerialAPI_SendImportantDevicesClear();

        if (sc <= 0)
        {
            WRN_PRINTF("GW KeepAlive: ImportantDevicesClear failed, return %d\n", sc); // NOSONAR
            /* sc==0 is NCP CLEAR_NOT_ACCEPTED (0x00); must not return GW_KEEPALIVE_OK -- upstream treats 0 as success. */
            if (sc < 0)
            {
                return sc;
            }
            return GW_KEEPALIVE_ERR_CLEAR_NOT_ACCEPTED;
        }
        LOG_PRINTF("GW KeepAlive: ImportantDevicesClear ok\n"); // NOSONAR
    }
    else
    {
        int sc = SerialAPI_SendImportantDeviceList(important_node_list, count, max_important_frame_bytes);

        if (sc != SERIALAPI_HOST_HIBERNATION_OK)
        {
            WRN_PRINTF("GW KeepAlive: SendImportantDeviceList failed, return %d\n", sc); // NOSONAR
            return sc;
        }
        LOG_PRINTF("GW KeepAlive: SendImportantDeviceList ok (count=%u)\n", (unsigned)count); // NOSONAR
    }

    return GW_KEEPALIVE_OK;
}

int gw_keepalive_handle_app_state_set(uint8_t state, uint8_t severity_level)
{
    if (state != GW_APP_STATE_ASLEEP && state != GW_APP_STATE_AWAKE)
    {
        return GW_KEEPALIVE_ERR_INVALID_APP_STATE;
    }

    app_state = state;
    LOG_PRINTF("GW KeepAlive: GATEWAY_APP_STATE_SET: %s\n", state ? "AWAKE" : "ASLEEP"); // NOSONAR

    /* Forward to NCP via SerialAPI */
    if (state == GW_APP_STATE_AWAKE)
    {
        int sc;
        sc = SerialAPI_RequestS2MessageCountList(0);
        if (sc != SERIALAPI_HOST_HIBERNATION_OK)
        {
            WRN_PRINTF("GW KeepAlive: RequestS2MessageCountList failed, return %d\n", sc); // NOSONAR
        }
        else
        {
            LOG_PRINTF("GW KeepAlive: RequestS2MessageCountList ok\n"); // NOSONAR
        }

        sc = SerialAPI_RequestWakeupReport();
        if (sc != SERIALAPI_HOST_HIBERNATION_OK)
        {
            WRN_PRINTF("GW KeepAlive: RequestWakeupReport failed, return %d\n", sc); // NOSONAR
            return sc;
        }
        LOG_PRINTF("GW KeepAlive: RequestWakeupReport ok\n"); // NOSONAR

        /* Spec: MUST clear important-device list on NCP before Host State AWAKE (0x00). */
        sc = SerialAPI_SendImportantDevicesClear();
        if (sc <= 0)
        {
            WRN_PRINTF("GW KeepAlive: ImportantDevicesClear (wake) failed, return %d\n", sc); // NOSONAR
            if (sc < 0)
            {
                return sc;
            }
            return GW_KEEPALIVE_ERR_CLEAR_NOT_ACCEPTED;
        }
        LOG_PRINTF("GW KeepAlive: ImportantDevicesClear (wake) ok\n"); // NOSONAR
        {
            uint8_t ncp_severity = 0;
            sc = SerialAPI_NotifyHostState(HOST_HIBERNATION_STATE_AWAKE, 0x00, &ncp_severity);
            if (sc != SERIALAPI_HOST_HIBERNATION_OK)
            {
                WRN_PRINTF("GW KeepAlive: NotifyHostState(AWAKE) failed, return %d\n", sc); // NOSONAR
                return sc;
            }
            LOG_PRINTF("GW KeepAlive: NotifyHostState(AWAKE) ok, NCP severity threshold=%u\n",
                       (unsigned)ncp_severity); // NOSONAR
        }
    }
    else
    {
        /* Save current S2 SPAN table before telling the NCP we sleep */
        sec2_persist_span_table();

        uint8_t ncp_severity = 0;
        int sc = SerialAPI_NotifyHostState(HOST_HIBERNATION_STATE_GOING_TO_SLEEP,
                                           severity_level, &ncp_severity);

        if (sc != SERIALAPI_HOST_HIBERNATION_OK)
        {
            WRN_PRINTF("GW KeepAlive: NotifyHostState(GOING_TO_SLEEP) failed, return %d\n", sc); // NOSONAR
            return sc;
        }
        LOG_PRINTF("GW KeepAlive: NotifyHostState(GOING_TO_SLEEP) ok, requested severity=%u, NCP severity threshold=%u\n",
                   (unsigned)severity_level, (unsigned)ncp_severity); // NOSONAR
        if (ncp_severity != severity_level)
        {
            WRN_PRINTF("GW KeepAlive: NCP severity threshold mismatch - requested=%u, NCP configured=%u\n",
                       (unsigned)severity_level, (unsigned)ncp_severity); // NOSONAR
            return GW_KEEPALIVE_ERR_SEVERITY_MISMATCH;
        }
    }

    return GW_KEEPALIVE_OK;
}

uint8_t gw_keepalive_get_app_state(void)
{
    return app_state;
}

const gw_important_node_entry_t *gw_keepalive_get_node_list(uint16_t *count)
{
    if (count)
    {
        *count = important_node_count;
    }
    return (important_node_count > 0) ? important_node_list : NULL;
}

void gw_keepalive_on_ncp_s2_count_sync(uint16_t node_id, uint8_t s2_count, uint8_t last_seq)
{
    if (sec2_reconcile_span_from_ncp(node_id, s2_count, last_seq))
    {
        if (s2_count > 0)
        {
            LOG_PRINTF("GW KeepAlive: S2 reconcile node=%u s2_count=%u last_seq=%u (SPAN updated, PRNG advanced)\n",
                       (unsigned)node_id, (unsigned)s2_count, (unsigned)last_seq); // NOSONAR
        }
        else
        {
            LOG_PRINTF("GW KeepAlive: S2 reconcile node=%u s2_count=%u last_seq=%u (rx_seq synced, PRNG not advanced)\n",
                       (unsigned)node_id, (unsigned)s2_count, (unsigned)last_seq); // NOSONAR
        }
    }
    else
    {
        DBG_PRINTF("GW KeepAlive: S2 reconcile node=%u s2_count=%u last_seq=%u (no matching SPAN)\n",
                   (unsigned)node_id, (unsigned)s2_count, (unsigned)last_seq); // NOSONAR
    }
}

void gw_keepalive_send_wake_notify(uint8_t wake_reason,
                                   const uint8_t *opt_data,
                                   uint16_t opt_data_len)
{
    uint8_t buf[GW_WAKE_NOTIFY_REPORT_HEADER_BYTES + GW_WAKE_NOTIFY_REPORT_OPT_DATA_MAX];
    uint16_t frame_len;
    zwave_connection_t conn;

    if (uip_is_addr_unspecified(&cfg.unsolicited_dest))
    {
        WRN_PRINTF("GW KeepAlive: wake notify dropped - no unsolicited destination configured\n"); // NOSONAR
        return;
    }

    if (uip_is_addr_unspecified(&cfg.lan_addr))
    {
        WRN_PRINTF("GW KeepAlive: wake notify dropped - lan_addr not set (DTLS source)\n"); // NOSONAR
        return;
    }

    if (opt_data_len > GW_WAKE_NOTIFY_REPORT_OPT_DATA_MAX)
    {
        opt_data_len = GW_WAKE_NOTIFY_REPORT_OPT_DATA_MAX;
    }

    buf[0] = COMMAND_CLASS_ZIP_GATEWAY;
    buf[1] = GATEWAY_WAKE_NOTIFY_REPORT;
    buf[2] = wake_reason;
    buf[3] = 0; /* flags - reserved */
    buf[4] = (uint8_t)(opt_data_len & 0xFF);
    buf[5] = (uint8_t)(opt_data_len >> 8);
    if (opt_data && opt_data_len > 0)
    {
        memcpy(&buf[6], opt_data, opt_data_len);
    }
    frame_len = (uint16_t)(GW_WAKE_NOTIFY_REPORT_HEADER_BYTES + opt_data_len);

    memset(&conn, 0, sizeof(conn));
    /* lipaddr aliases sipaddr in uip_udp_conn; must match LAN source for inbound DTLS replies. */
    uip_ipaddr_copy(&conn.lipaddr, &cfg.lan_addr);
    uip_ipaddr_copy(&conn.ripaddr, &cfg.unsolicited_dest);

    ClassicZIPNode_SendUnsolicited(&conn,
                                  (ZW_APPLICATION_TX_BUFFER *)buf,
                                  (BYTE)frame_len,
                                  &cfg.unsolicited_dest,
                                  UIP_HTONS(cfg.unsolicited_port),
                                  TRUE);

    LOG_PRINTF("GW KeepAlive: issued GATEWAY_WAKE_NOTIFY_REPORT (reason=%u)\n",
               (unsigned)wake_reason); // NOSONAR
}

void gw_keepalive_on_device_lost(const uint8_t *payload, uint16_t payload_len)
{
    if (payload != NULL && payload_len > 0)
    {
        /* NCP: n × (node_id u16 BE, last_seen_min u16 BE) — same as test_serialapi / S2 host-hibernation tuples. */
        if ((payload_len % 4u) != 0u)
        {
            WRN_PRINTF("GW KeepAlive: device lost bad len=%u raw %s\n", (unsigned)payload_len,
                       print_frame((const char *)payload, (unsigned)payload_len)); // NOSONAR
        }
        else
        {
            for (uint16_t i = 0; i < payload_len; i += 4u)
            {
                LOG_PRINTF("GW KeepAlive: device lost node_id=%u last_seen_min=%u\n",
                           (unsigned)(((uint16_t)payload[i] << 8) | payload[i + 1]),
                           (unsigned)(((uint16_t)payload[i + 2] << 8) | payload[i + 3])); // NOSONAR
            }
        }
    }
    else
    {
        LOG_PRINTF("GW KeepAlive: device lost (no NCP payload)\n"); // NOSONAR
    }

    gw_keepalive_send_wake_notify(GW_WAKE_REASON_DEVICE_LOST, payload, payload_len);
}
