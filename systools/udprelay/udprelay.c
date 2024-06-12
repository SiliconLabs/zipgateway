/* © 2014 Silicon Laboratories Inc.
 */

#include<stdio.h>

int dbg_lvl = 0;

#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <net/ethernet.h>
#include <net/if.h>

#include <arpa/inet.h>

#include <pcap.h>
#include <unistd.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>

struct forward_entry
{
  struct forward_entry *next;
  uint16_t port;
  int family;
  int fd;
};

static struct forward_entry* entries;
static char* output_interface;
static char* input_interface;
static pcap_t* pcap;

int
pcap_init(const char* if_name)
{
  char pcap_errbuf[PCAP_ERRBUF_SIZE];
  pcap_errbuf[0] = '\0';
  pcap = pcap_open_live(if_name, 96, 0, 0, pcap_errbuf);
  if (pcap_errbuf[0] != '\0')
  {
    fprintf(stderr, "%s\n", pcap_errbuf);
  }

  if (!pcap)
  {
    return -1;
  };
  return 0;
}

int
create_udp_socket(uint16_t port, int family)
{
  int rc;
  const int on = 1, off = 0;
  int sockfd;
  sockfd = socket(family, SOCK_DGRAM, IPPROTO_UDP);

  if (sockfd < 0)
  {
    perror("Unable to create raw socket");
    return -1;
  }

  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  if(input_interface) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(struct ifreq));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name),"%s", input_interface);
    ioctl(sockfd, SIOCGIFINDEX, &ifr);
    setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE,  (void*)&ifr, sizeof(struct ifreq));
  }

  if (family == AF_INET6)
  {
    struct sockaddr_in6 sin6;

    /**
     * http://stackoverflow.com/questions/3062205/setting-the-source-ip-for-a-udp-socket
     */
    setsockopt(sockfd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof(on));
    setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));

    memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = htons(port);

    rc = bind(sockfd, (struct sockaddr*) &sin6, sizeof(sin6));
  }
  else
  {
    struct sockaddr_in sin;

    setsockopt(sockfd, IPPROTO_IP, IP_PKTINFO, &on, sizeof(on));

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    rc = bind(sockfd, (struct sockaddr*) &sin, sizeof(sin));
  }

  if (rc < 0)
  {
    perror("Unable to bind to socket");
    close(sockfd);
    return -1;
  }
  return sockfd;
}

void
hexdump(uint8_t* buf, int size)
{
  int i;
  for (i = 0; i < size; i++)
  {
    printf("%2.2x", buf[i]);

    if ((i & 0xF) == 0xF)
    {
      printf("\n");
    }
  }
  printf("\n");
}

size_t
recvfromandto(int socket, void * buffer, size_t length, int flags, struct sockaddr * from, socklen_t * from_len,
    struct sockaddr * to, socklen_t * to_len)
{

  int bytes_received;
  struct iovec iovec[1];
  struct msghdr msg;
  char msg_control[1024];
  struct cmsghdr* cmsg;

  iovec[0].iov_base = buffer;
  iovec[0].iov_len = length;
  msg.msg_name = from;
  msg.msg_namelen = 128;
  msg.msg_iov = iovec;
  msg.msg_iovlen = sizeof(iovec) / sizeof(*iovec);
  msg.msg_control = msg_control;
  msg.msg_controllen = sizeof(msg_control);
  msg.msg_flags = 0;
  bytes_received = recvmsg(socket, &msg, 0);

  for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != 0; cmsg = CMSG_NXTHDR(&msg, cmsg))
  {
    if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_PKTINFO)
    {
      struct in_pktinfo in_pktinfo = *(struct in_pktinfo*) CMSG_DATA(cmsg);
      struct sockaddr_in* sa = (struct sockaddr_in*) to;
      sa->sin_family = AF_INET;
      sa->sin_port = 0;
      memcpy(&sa->sin_addr, &in_pktinfo.ipi_addr, sizeof(struct in_addr));
      *from_len = sizeof(struct sockaddr_in);
      *to_len = sizeof(struct sockaddr_in);
    }
    if (cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_PKTINFO)
    {
      struct in6_pktinfo in6_pktinfo = *(struct in6_pktinfo*) CMSG_DATA(cmsg);

      struct sockaddr_in6* sa6 = (struct sockaddr_in6*) to;
      //sa6->sin6_len = sizeof(struct sockaddr_in6);
      sa6->sin6_family = AF_INET6;
      sa6->sin6_port = 0;
      memcpy(&sa6->sin6_addr, &in6_pktinfo.ipi6_addr, sizeof(struct in6_addr));
      *from_len = sizeof(struct sockaddr_in6);
      *to_len = sizeof(struct sockaddr_in6);
    }
  }
  return bytes_received;
}

#define PROTO_UDP   17

static uint8_t my_mac[] =
  { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
const uint8_t multicast_ipv4_mac[] =
  { 0x01, 0x00, 0x5e, 0x00, 0x00, 0xfb };
const uint8_t multicast_ipv6_mac[] =
  { 0x33, 0x33, 0x00, 0x00, 0x00, 0xfb };
const uint8_t broadcast_mac[] =
  { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/*
 Generic checksum calculation function
 */
uint16_t
csum(uint16_t *ptr, int nbytes)
{
  register int32_t sum;
  uint16_t oddbyte;
  uint16_t answer;

  sum = 0;
  while (nbytes > 1)
  {
    sum += *ptr++;
    nbytes -= 2;
  }
  if (nbytes == 1)
  {
    oddbyte = 0;
    *((u_char*) &oddbyte) = *(u_char*) ptr;
    sum += oddbyte;
  }

  sum = (sum >> 16) + (sum & 0xffff);
  sum = sum + (sum >> 16);
  answer = (int16_t) ~sum;

  return (answer);
}

size_t
build_ipv6_udp_frame(struct sockaddr_in6 *from, struct sockaddr_in6 *to, uint8_t* udp_data, size_t udp_data_len,
    uint8_t* ipv6_data)
{
  struct ipv6_pseudo_header
  {
    struct in6_addr ip6_src; /* source address */
    struct in6_addr ip6_dst; /* destination address */
    uint16_t dummy1;
    uint16_t udp_length;
    uint32_t next_hdr;
  };
  uint8_t psh_buffer[1500];

  struct ether_header *eth = (struct ether_header*) ipv6_data;
  struct ip6_hdr* ip_hdr = (struct ip6_hdr*) (ipv6_data + sizeof(struct ether_header));
  struct udphdr *udph = (struct udphdr *) (ipv6_data + sizeof(struct ip6_hdr) + sizeof(struct ether_header));

  memset(ipv6_data, 0, sizeof(struct ip6_hdr) + sizeof(struct udphdr) + sizeof(struct ether_header));

  eth->ether_type = htons(ETHERTYPE_IPV6);
  memcpy(eth->ether_dhost, multicast_ipv6_mac, ETHER_ADDR_LEN);
  memcpy(eth->ether_shost, my_mac, ETHER_ADDR_LEN);

  ip_hdr->ip6_ctlun.ip6_un1.ip6_un1_flow = htonl(0x60000000L);
  ip_hdr->ip6_ctlun.ip6_un1.ip6_un1_nxt = PROTO_UDP;
  ip_hdr->ip6_ctlun.ip6_un1.ip6_un1_plen = ntohs(sizeof(struct udphdr) + udp_data_len);

  memcpy(&ip_hdr->ip6_dst, &to->sin6_addr, sizeof(struct in6_addr));
  memcpy(&ip_hdr->ip6_src, &from->sin6_addr, sizeof(struct in6_addr));

  udph->dest = to->sin6_port;
  udph->source = from->sin6_port;
  udph->check = 0x0;
  udph->len = ntohs(udp_data_len + sizeof(struct udphdr));

  memcpy((uint8_t*) udph + sizeof(struct udphdr), udp_data, udp_data_len);

  /* Now we need to calculate the UDP checksum using the pseudo header */
  struct ipv6_pseudo_header* psh = (struct ipv6_pseudo_header*) (psh_buffer);
  memcpy(&psh->ip6_dst, &ip_hdr->ip6_dst, sizeof(struct in6_addr));
  memcpy(&psh->ip6_src, &ip_hdr->ip6_src, sizeof(struct in6_addr));
  psh->dummy1 = 0;
  psh->udp_length = ip_hdr->ip6_ctlun.ip6_un1.ip6_un1_plen;
  psh->next_hdr = htonl(PROTO_UDP);
  memcpy(psh_buffer + sizeof(struct ipv6_pseudo_header), udph, udp_data_len + sizeof(struct udphdr));

  udph->check = csum((uint16_t*) psh, udp_data_len + sizeof(struct udphdr) + sizeof(struct ipv6_pseudo_header));
  return udp_data_len + sizeof(struct udphdr) + sizeof(struct ip6_hdr) + sizeof(struct ether_header);
}

size_t
build_ipv4_udp_frame(struct sockaddr_in *from, struct sockaddr_in *to, uint8_t* udp_data, size_t udp_data_len,
    uint8_t* ipv4_data)
{
  struct ipv4_pseudo_header
  {
    struct in_addr ip_src; /* source address */
    struct in_addr ip_dst; /* destination address */
    uint8_t dummy;
    uint8_t protocol;
    uint16_t udp_length;
  };
  uint8_t psh_buffer[1500];

  struct ether_header *eth = (struct ether_header*) ipv4_data;
  struct ip* ip_hdr = (struct ip*) (ipv4_data + sizeof(struct ether_header));
  struct udphdr *udph = (struct udphdr *) (ipv4_data + sizeof(struct ip) + sizeof(struct ether_header));

  memset(ipv4_data, 0, sizeof(struct ip) + sizeof(struct udphdr) + sizeof(struct ether_header));

  eth->ether_type = htons(ETHERTYPE_IP);
  memcpy(eth->ether_dhost, multicast_ipv4_mac, ETHER_ADDR_LEN);
  memcpy(eth->ether_shost, my_mac, ETHER_ADDR_LEN);

  ip_hdr->ip_v = 0x4;
  ip_hdr->ip_hl = 0x5;
  ip_hdr->ip_dst = to->sin_addr;
  ip_hdr->ip_src = from->sin_addr;
  ip_hdr->ip_len = ntohs(sizeof(struct udphdr)+ sizeof(struct ip) + udp_data_len);
  ip_hdr->ip_p = PROTO_UDP;
  ip_hdr->ip_sum = 0x0;
  ip_hdr->ip_ttl = 64;
  ip_hdr->ip_sum = csum((uint16_t*)ip_hdr, sizeof(struct ip));

  udph->dest = to->sin_port;
  udph->source = from->sin_port;
  udph->check= 0x0;
  udph->len = ntohs(udp_data_len + sizeof(struct udphdr));

  memcpy((uint8_t*) udph + sizeof(struct udphdr), udp_data, udp_data_len);

  /* Now we need to calculate the UDP checksum using the pseudo header */
  struct ipv4_pseudo_header* psh = (struct ipv4_pseudo_header*) (psh_buffer);
  psh->ip_dst = ip_hdr->ip_dst;
  psh->ip_src = ip_hdr->ip_src;
  psh->udp_length = udph->len;
  psh->protocol = PROTO_UDP;
  psh->dummy = 0;
  memcpy(psh_buffer + sizeof(struct ipv4_pseudo_header), udph, udp_data_len + sizeof(struct udphdr));

  udph->check = csum((uint16_t*) psh, udp_data_len + sizeof(struct udphdr) + sizeof(struct ipv4_pseudo_header));
  return udp_data_len + sizeof(struct udphdr) + sizeof(struct ip) + sizeof(struct ether_header);
}

void usage() {
  fprintf(stderr,"Usage:\n");
  fprintf(stderr,"udprelay -i <output interface> [-b <input interface>] [-4 port]+ [-6 port]+ ... >\n");

}

int
get_mac_of_interface(const char* ifname)
{
  struct ifreq ifr;

  int sock;
  sock = socket(PF_INET, SOCK_STREAM, 0);
  if (-1 == sock)
  {
    perror("socket() ");
    return 1;
  }

  strncpy(ifr.ifr_name,ifname,sizeof(ifr.ifr_name)-1);
  ifr.ifr_name[sizeof(ifr.ifr_name)-1]='\0';


  if (-1 == ioctl(sock, SIOCGIFHWADDR, &ifr))
  {
    perror("ioctl(SIOCGIFHWADDR) ");
    return 1;
  }
  memcpy(&my_mac,ifr.ifr_ifru.ifru_hwaddr.sa_data, 6);
  close(sock);
  return 0;
}


int
main(int argc, char** argv)
{
  uint8_t buffer[1500];
  uint8_t send_buffer[1500];
  size_t send_length;
  int len;
  struct forward_entry* e;

  int ch;

  pcap = 0;
  entries = 0;
  output_interface =0;
  input_interface = 0;
  while ((ch = getopt(argc, argv, "4:6:i:b:")) != -1)
  {
    switch (ch)
    {
    case '4':
    case '6':

      e = (struct forward_entry*) malloc(sizeof(struct forward_entry));
      e->next = entries;
      entries = e;
      e->port = atoi(optarg);
      e->family = ch == '4' ? AF_INET : AF_INET6;
      e->fd = create_udp_socket(e->port, e->family);
      if (e->fd < 0)
      {
        goto done;
      }
      printf("Listening on port %i %s\n", e->port, e->family == AF_INET ? "ipv4": "ipv6");
      break;
    case 'i':
      output_interface = optarg;
      break;
    case 'b':
      input_interface = optarg;
      break;
    case '?':
    default:
      usage();
      goto done;
    }
  }

  if (output_interface == 0)
  {
    usage();
    goto done;
  }

  get_mac_of_interface(output_interface);
  if (pcap_init(output_interface) < 0)
  {
    goto done;
  }

  printf("Sending on %s HWADDR: %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n", output_interface, my_mac[0],my_mac[1],my_mac[2],my_mac[3],my_mac[4],my_mac[5]);
  while (1)
  {
    struct sockaddr_in6 from, to;
    socklen_t from_len, to_len;

    fd_set rfds;
    struct timeval tv;
    int retval;
    int fd_max;
    FD_ZERO(&rfds);
    fd_max = 0;
    for (e = entries; e; e = e->next)
    {
      FD_SET(e->fd, &rfds);
      if (e->fd > fd_max)
        fd_max = e->fd;
    };

    tv.tv_sec = 5;
    tv.tv_usec = 0;

    retval = select(fd_max + 1, &rfds, NULL, NULL, &tv);
    /* Don't rely on the value of tv now! */

    if (retval == -1)
    {
      perror("select()");
      break;
    }
    else if (retval)
    {
      for (e = entries; e; e = e->next)
      {
        if (FD_ISSET(e->fd, &rfds))
        {
          len = recvfromandto(e->fd, buffer, sizeof(buffer), 0, (struct sockaddr *) &from, &from_len,
              (struct sockaddr *) &to, &to_len);

          to.sin6_port = htons(e->port);
          if (to.sin6_family == AF_INET6)
          {
            send_length = build_ipv6_udp_frame(&from, &to, buffer, len, send_buffer);
          }
          else if (to.sin6_family == AF_INET)
          {
            send_length = build_ipv4_udp_frame( (struct sockaddr_in*)&from, (struct sockaddr_in*)&to, buffer, len, send_buffer);
          }
          else
          {
            send_length = 0;
          }

          if (send_length)
          {
            if (pcap_inject(pcap, send_buffer, send_length) < 0)
            {
              fprintf(stderr, "unable to send packet\n");
              goto done;
            }
          }
        }
      }
    }
  }

  done:
  if (pcap)
  {
    pcap_close(pcap);
  }
  /*Free up the entries*/
  while (entries)
  {
    e = entries;
    close(e->fd);
    free(e);
    entries = entries->next;
  }
}

