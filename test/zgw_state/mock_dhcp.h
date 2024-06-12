/* © 2020 Silicon Laboratories Inc. */

#include <stdint.h>

bool ip_packet_is_dhcp(uint8_t *pkt);

void mock_dhcp_server(uint8_t *pkt);
