/* © 2014 Silicon Laboratories Inc.
 */


#include "DTLS_server.h"
#include "contiki-net.h"
#include "memb.h"
#include "etimer.h"
#include "uip-debug.h"
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "TYPES.H"
#include "zip_router_ipv6_utils.h" /* nodeOfIP */
#include "zip_router_config.h"
#include "zw_network_info.h"
#include "ZW_udp_server.h"
#include "ClassicZIPNode.h"
#include "ZIP_Router_logging.h"

#define UIP_IP_BUF ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF ((struct uip_udp_hdr *)&uip_buf[UIP_LLIPH_LEN])

PROCESS(dtls_server_process, "DTLS server process");
static struct uip_udp_conn *server_conn;

static SSL_CTX* ctx = 0;
static SSL_CTX* client_ctx = 0;
int dtls_ssl_read_failed;

/*Session timeout in seconds */
#define DTLS_TIMEOUT 60

/*DTLS handshake timeout*/
#define DTLS_HANDSHAKE_TIMEOUT 3

struct dtls_session
{
  list_t list;
  struct uip_udp_conn conn;

  SSL* ssl; //The OpenSSL session

#ifdef USE_CYASSL
  enum
  { CYA_ACCEPT,CYA_READY}state;
#else
  BIO* rbio;
  BIO* wbio;
#endif
  struct uip_packetqueue_handle queue; //Queue used to buffer packages while handshake is in progress
  clock_t timeout;
  void (*cbFunc)(uint8_t b, void* user);
  void *user;
};

static void
dtls_free_session(struct dtls_session*s);

static void
dtls_session_timer_adjust();

#ifdef STATIC_SESSIONS
MEMB(sessions_memb, struct dtls_session, 10);
#else
#include<stdlib.h>
#endif
LIST(session_list);
static struct etimer timer;

#ifdef USE_CYASSL

#include <ctaocrypt/sha.h>

static int UserReceive(CYASSL *ssl, char *buf, int sz, void *ctx)
{
  int len;
  len = uip_datalen();
  if(len > 0)
  {
    DBG_PRINTF("Cyassl READ size %i of %i \n",len,sz);
    memcpy(buf,uip_appdata,uip_datalen());
    uip_len = 0;
    return len;
  }
  else
  {
    return CYASSL_CBIO_ERR_WANT_READ;
  }
}

static int UserSend(CYASSL* ssl, char *buf, int sz, void *ctx)
{

  struct dtls_session* s;

  s = (struct dtls_session*) ctx;

  DBG_PRINTF("Cyassl WRITE srcport %i dst port %i\n",s->conn.lport,s->conn.rport);
  s->conn.ttl = 64; //Reset the TTL to 64
  uip_udp_packet_send(&s->conn, buf, sz);
  return sz;
}

/* The DTLS Generate Cookie callback
 *  return : number of bytes copied into buf, or error
 */
int UserGenCookie(CYASSL* ssl, unsigned char* buf, int sz,void* ctx)
{
  struct dtls_session*s;
  Sha sha;
  byte digest[SHA_DIGEST_SIZE];
  int ret = 0;

  s = (struct dtls_session*) ctx;
  ret = InitSha(&sha);
  if (ret != 0)
  {
    return ret;
  }
  ShaUpdate(&sha, (const byte*) &s->conn.ripaddr, sizeof(uip_ipaddr_t));
  ShaFinal(&sha, digest);

  if (sz > SHA_DIGEST_SIZE)
  {
    sz = SHA_DIGEST_SIZE;
  }
  XMEMCPY(buf, digest, sz);

  return sz;
}

static void dtls_output(struct dtls_session*s)
{
  /*stub*/
}
#else
/* Sends a package from the SSL stack if needed */
static void
dtls_output(struct dtls_session*s)
{
  char read_buf[1500];
  int len;
  len = BIO_read(s->wbio, read_buf, sizeof(read_buf));
  if (len > 0)
  {
    //DBG_PRINTF("DTLS: protocol output len=%i\n",len);
    s->conn.ttl = 64; //Reset the TTL to 64
    uip_udp_packet_send(&s->conn, &read_buf, len);
  }
}
#endif

static void dtls_print_session_msg(const char* msg,struct dtls_session*s) {

  DBG_PRINTF("DTLS: %s\n", msg);
  uip_udp_print_conn(&s->conn);
  DBG_PRINTF("\n");
}


//const unsigned char PSK2[] = {0xaa,0xaa,0x00,0x00};
static unsigned int
psk_server_callback(SSL *ssl, const char *identity, unsigned char *psk,
    unsigned int max_psk_len)
{
  memcpy(psk, cfg.psk, cfg.psk_len);

  DBG_PRINTF("PSK callback identity:%s PSK len %i\n", identity, cfg.psk_len);

  return cfg.psk_len;
}

static unsigned int
psk_client_callback(SSL *ssl, const char *hint, char *identity,
    unsigned int max_identity_len, unsigned char *psk, unsigned int max_psk_len)
{
  memcpy(psk, cfg.psk, cfg.psk_len);
  strcpy(identity, "Client_identity");

  return cfg.psk_len;
}

/**
 * Callback from the SSL stack. This gives status updates when the
 * state of the connection changes for client connections.
 */
static void
client_info_callback(const SSL* ssl, int where, int ret)
{
  struct dtls_session*s;
  //DBG_PRINTF("INFO CALLBACK %x %x\n",where,ret);

  for (s = list_head(session_list); s; s = list_item_next(s))
  {
    if (ssl == s->ssl)
    {
      break;
    }
  }

  if (!s)
  {
    return;
  }

  /*
   * dont do this, we are polling the state in the main event loop*/
  if (where & SSL_CB_ALERT)
  {
    process_post(&dtls_server_process, DTLS_CONNECTION_CLOSE_EVENT, s);
  }
  else if (where == SSL_CB_HANDSHAKE_DONE)
  {
    process_post(&dtls_server_process, DLTS_CONNECTION_INIT_DONE_EVENT, s);

  }
}


/**
 * Create a new DTLS session from the given ip connection
 *
 * return A pointer to the new session, NULL if a session could not be allocated
 */
static struct dtls_session*
dtls_new_session(struct uip_udp_conn* conn, u8_t client)
{
  struct dtls_session*s;
#ifdef STATIC_SESSIONS
  s = memb_alloc(&sessions_memb);
#else
  s = (struct dtls_session*) malloc(sizeof(struct dtls_session) );
#endif
  if (!s)
  {
    ERR_PRINTF("NO more DTLS sessions available\n");
    return NULL;
  }
  DBG_PRINTF("New %s session allocated \n", client ? "client" : "server");
  s->conn = *conn;
  uip_udp_print_conn(&s->conn);
  s->ssl = SSL_new(client ? client_ctx : ctx);
  SSL_set_info_callback(s->ssl, client_info_callback);

  s->timeout = clock_seconds() + DTLS_HANDSHAKE_TIMEOUT;
  uip_packetqueue_new(&s->queue);
  if (!s->ssl)
  {
    ERR_PRINTF("Unable to allocate a DTLS session\n");
#ifdef STATIC_SESSIONS
    memb_free(&sessions_memb, s);
#else
    free(s);
#endif
    return NULL;
  }

#ifdef USE_CYASSL
  CyaSSL_SetIOReadCtx(s->ssl,s);
  CyaSSL_SetIOWriteCtx(s->ssl,s);
  CyaSSL_SetCookieCtx(s->ssl,s);
  s->state = CYA_ACCEPT;
#else
  s->rbio = BIO_new(BIO_s_mem());
  BIO_set_nbio(s->rbio, 1);
  s->wbio = BIO_new(BIO_s_mem());
  BIO_set_nbio(s->wbio, 1);
  SSL_set_bio(s->ssl, s->rbio, s->wbio);

  if (client)
  {
    SSL_set_connect_state(s->ssl);

    if (SSL_do_handshake(s->ssl) <= 0)
    {
      //ERR_PRINTF("Error: %s\n", ERR_reason_error_string(ERR_get_error()));
    }
  }
  else
  {
    SSL_set_accept_state(s->ssl);
  }

#endif

  list_add(session_list, s);
  return s;
}

/**
 * Locate a DTLS session give the connection parameters
 * @return The session if one could be found, NULL otherwise.
 */
static struct dtls_session*
dtls_find_session(struct uip_udp_conn* conn)
{
  struct dtls_session*s;

  for (s = list_head(session_list); s; s = list_item_next(s))
  {
    if ( memcmp(&s->conn,conn,offsetof(struct uip_udp_conn,ttl) ) == 0)
    {
      return s;
    }
  }
  return NULL;
}

int
dtls_send(struct uip_udp_conn* conn, const void* data, u16_t len,
    u8_t create_new, void (*cbFunc)(uint8_t, void*), void *user)
{
  struct dtls_session* s;

  int r;
  s = dtls_find_session(conn);

  /*Create a new client connection*/
  if (!s)
  {
    if (create_new)
    {
      s = dtls_new_session(conn, 1);
      process_post(&dtls_server_process,DLTS_SESSION_UPDATE_EVENT,0);
      if(!s) {
        return 0;
      }
    }
    else
    {
      return 0;
    }
  }

  s->user = user;
  s->cbFunc = cbFunc;
  /* If handshake is not yet done queue the packet */
  if (!SSL_is_init_finished(s->ssl))
  {
    if (!uip_packetqueue_alloc(&s->queue, (u8_t*) data, len, 3*CLOCK_SECOND))
    {
      ERR_PRINTF("No room for SSL payload, handshake is still not done.");
    }
    dtls_output(s);
    return len;
  }

  s->timeout = clock_seconds() + DTLS_TIMEOUT;
  process_post(&dtls_server_process,DLTS_SESSION_UPDATE_EVENT,0);
  if ((r = SSL_write(s->ssl, data, len)) > 0)
  {
    //DBG_PRINTF("Sending encrypted data\n");
    if (cbFunc) {
      cbFunc(0, user);
    }
    dtls_output(s);
    return r;
  }

  dtls_print_session_msg("Unable to send to DTLS client",s);
  return 0;
}

/**
 * Free up memory allocated by a session.
 * And send the SSL alert message
 */
static void
dtls_free_session(struct dtls_session*s)
{
  list_remove(session_list, s);

  if(s->ssl) {
    if (SSL_is_init_finished(s->ssl))
    {
      SSL_shutdown(s->ssl);
      dtls_output(s);
    }

    SSL_free(s->ssl); //This also frees BIOs
    s->ssl = 0;
    dtls_print_session_msg("Closing DTLS connection",s);
  }

  while (uip_packetqueue_buf(&s->queue))
  {
    uip_packetqueue_pop(&s->queue);
  }

#ifdef STATIC_SESSIONS
    memb_free(&sessions_memb, s);
#else
    free(s);
#endif
}

/**
 * Adjust the event timer to match the next timeout event
 */
static void
dtls_session_timer_adjust()
{
  struct dtls_session*s;
  clock_t min = clock_seconds() + DTLS_TIMEOUT + 1;

  if (list_head(session_list))
  {
    for (s = list_head(session_list); s; s = list_item_next(s))
    {
      if (min > s->timeout){
        min = s->timeout;
      }
    }

    min = min - clock_seconds();

    //DBG_PRINTF("Next timeout is in %i seconds\n",min);
    if(min<=0) {
      etimer_set(&timer, 50);
    }else {
      etimer_set(&timer, min * CLOCK_CONF_SECOND);
    }
    etimer_restart(&timer);
  }
}


int session_done_for_uipbuf() {
  struct uip_udp_conn *c = get_udp_conn();
  struct dtls_session* s;

  s = dtls_find_session(c);
  return (s && SSL_is_init_finished(s->ssl));
}

void dtls_close_all()
{
  struct dtls_session* s;
  while ((s = list_head(session_list)) != NULL)
  {
    dtls_free_session(s);
  }

}

#include "serial-line.h"

PROCESS_THREAD(dtls_server_process, ev, data)
{
  PROCESS_BEGIN()
    ;
    u8_t read_buf[UIP_BUFSIZE];

    struct dtls_session* s, *next;
    int len;

    if (ctx == NULL)
    {
#ifdef STATIC_SESSIONS
      memb_init(&sessions_memb);
#endif
      list_init(session_list);

      SSL_library_init();
      //OpenSSL_add_ssl_algorithms();
      SSL_load_error_strings();

      /* Create the SSL server context  */
      if ((ctx = SSL_CTX_new(DTLS_server_method())) == NULL)
      {
        fprintf(stderr, "SSL_CTX_new error.\n");
        return 0;
      }

      /* Create the SSL client context */
      if ((client_ctx = SSL_CTX_new(DTLS_client_method())) == NULL)
      {
        fprintf(stderr, "SSL_CTX_new error.\n");
        return 0;
      }

      if (!SSL_CTX_use_certificate_file(ctx, cfg.cert, SSL_FILETYPE_PEM))
      {
        ERR_PRINTF("Error loading certificate, please check the file.\n");
      }

      if (!SSL_CTX_use_PrivateKey_file(ctx, cfg.priv_key, SSL_FILETYPE_PEM))
      {
        ERR_PRINTF("Error loading key, please check the file.\n");
      }

      if (!SSL_CTX_set_cipher_list(ctx, "PSK"))
      {
        ERR_PRINTF("Unable to select ciphers\n");
      }

      if (!SSL_CTX_set_cipher_list(client_ctx, "PSK"))
      {
        ERR_PRINTF("Unable to select ciphers\n");
      }

      SSL_CTX_set_read_ahead(ctx, 1);
      SSL_CTX_set_read_ahead(client_ctx, 1);

      /*Setup PSK callback */
      SSL_CTX_set_psk_server_callback(ctx, psk_server_callback);
      SSL_CTX_set_psk_client_callback(client_ctx, psk_client_callback);

      /*SSL_CTX_use_psk_identity_hint(ctx,"HelloWorld");*/
#ifdef USE_CYASSL

      CyaSSL_Debugging_ON();
      CyaSSL_SetIORecv(ctx,UserReceive);
      CyaSSL_SetIOSend(ctx,UserSend);
      CyaSSL_CTX_SetGenCookie(ctx,UserGenCookie);
#endif
    }

    server_conn = udp_new(NULL, 0, NULL);
    udp_bind(server_conn, UIP_HTONS(DTLS_PORT));

    LOG_PRINTF("DTLS server started\n");
    while (1)
    {
      PROCESS_WAIT_EVENT()
      ;

      if(ev == DLTS_SESSION_UPDATE_EVENT) {
        dtls_session_timer_adjust();
      }
      else if (ev == PROCESS_EVENT_TIMER)
      { /*Check for session timeouts*/
        s = list_head(session_list);
        while (s)
        {
          next = list_item_next(s);
          if (s->timeout <= clock_seconds())
          {
            dtls_print_session_msg("Session timeout",s);
            dtls_free_session(s);
            dtls_session_timer_adjust();
          }
          s = next;
        }
      }
      else if ((ev == tcpip_event && uip_newdata())|| (ev==DTLS_SERVER_INPUT_EVENT) ){
      struct uip_udp_conn *c = get_udp_conn();

      //DBG_PRINTF("DTLS input: ev: %x\n", ev);
      if( ((u8_t*)uip_appdata)[0] == 0x16) { //Only if content type is Handshake
        DBG_PRINTF("DTLS input is handshake:\n");
      }
      memset(&server_conn->ripaddr, 0, sizeof(uip_ipaddr_t));
      memset(&server_conn->sipaddr, 0, sizeof(uip_ipaddr_t));

      if( ((u8_t*)uip_appdata)[0] == 0x16) { //Only accept create new session if content type is Handshake
          switch (((u8_t*)uip_appdata)[13]) {
              case 0x01:
                    DBG_PRINTF("DTLS handshake Client hello\n");
                    break;
              case 0x02:
                    DBG_PRINTF("DTLS handshake Server hello\n");
                    break;
              case 0x03:
                    DBG_PRINTF("DTLS handshake Hello verify request\n");
                    break;
              case 0x04:
                    DBG_PRINTF("DTLS handshake New Session Ticket\n");
                    break;
              case 0x10:
                    DBG_PRINTF("DTLS handshake Client Key Exchange\n");
                    break;
              default:
                    DBG_PRINTF("DTLS handshake %x\n", ((u8_t*)uip_appdata)[13]);
                    break;
         }
      }
      s = dtls_find_session(c);
      if(!s)
      {
        if( ((u8_t*)uip_appdata)[0] == 0x16) { //Only accept create new session if content type is Handshake
          s= dtls_new_session(c,0);
        }

        if(!s)
        {
          continue;
        }
      } else {
        s->timeout = clock_seconds() + DTLS_TIMEOUT;
      }
      dtls_session_timer_adjust();

#ifdef USE_CYASSL
          if(s->state == CYA_ACCEPT)
          {
            DBG_PRINTF("Accepting ....\n");
            int rc = CyaSSL_accept(s->ssl);
            if(rc == SSL_SUCCESS)
            {
              s->state = CYA_READY;
            }
            else
            {
              if( CyaSSL_get_error(s->ssl,rc) != SSL_ERROR_WANT_READ)
              {
                ERR_PRINTF("CYASSL failed handshake code %i\n", CyaSSL_get_error(s->ssl,rc));
                dtls_free_session(s);
                continue;
              }
            }
          }
#else
          len = BIO_write(s->rbio,uip_appdata,uip_datalen());
          if(len> 0)
          {
            //DBG_PRINTF("We have written %i bytes to SSL stack\n",len);
            /*SSL_do_handshake(s->ssl);*/
          }
          else
          {
            dtls_print_session_msg("BIO Write failed",s);
          }
#endif
          dtls_ssl_read_failed = FALSE;
          len = SSL_read(s->ssl,read_buf,sizeof(read_buf));
          if(len>0)
          {
            int node = nodeOfIP(&s->conn.sipaddr);
            //DBG_PRINTF("SSL DATA len %i\n",len);

            if(node == MyNodeID)
            {
              UDPCommandHandler(c,read_buf, len,TRUE);
            }
            else
            {
              ClassicZIPNode_dec_input(read_buf, len);
            }
          }
          else
          {
            dtls_ssl_read_failed = TRUE;
            /* Do "man SSL_get_error" to understand following error codes*/
            dtls_print_session_msg("DTLS read failed: %d",s);
            switch (SSL_get_error(s->ssl, len)) {
                case SSL_ERROR_WANT_READ:
                    DBG_PRINTF("SSL_ERROR_WANT_READ: Processing was not completed successfully because there was no data available for reading,\n");
                    break;
                case SSL_ERROR_WANT_WRITE:
                    ERR_PRINTF("SSL_ERROR_WANT_WRTIE: Processing was not completed successfully because the socket associated with the SSL session is blocked from sending data.\n");
                    break;
                case SSL_ERROR_SYSCALL:
                    ERR_PRINTF("SSL_ERROR_SYSCALL: An I/O error occurred. \n");
                    break;
                case SSL_ERROR_ZERO_RETURN:
                    DBG_PRINTF("SSL_ERROR_ZERO_RETURN: The remote application shut down the SSL connection normally.");
                    break;
                case SSL_ERROR_WANT_CONNECT:
                    ERR_PRINTF("SSL_ERROR_WANT_CONNECT: Processing was not completed successfully because the SSL session was in the process of starting the session, but it has not completed yet.\n");
                    break;
            }
          }
          dtls_output(s);
        }
        else if (ev == DTLS_CONNECTION_CLOSE_EVENT)
        {
          dtls_free_session((struct dtls_session*) data);
        }
        else if (ev == DLTS_CONNECTION_INIT_DONE_EVENT)
        {

          s = (struct dtls_session*) data;
          dtls_print_session_msg("Client handshake done",s);
          /* Send queued packages */
          while (uip_packetqueue_buf(&s->queue))
          {
            SSL_write(s->ssl, uip_packetqueue_buf(&s->queue),
                uip_packetqueue_buflen(&s->queue));
            if (s->cbFunc) {
              s->cbFunc(0, s->user);
            }
            dtls_output(s);
            uip_packetqueue_pop(&s->queue);
          }
        }
      }

      SSL_CTX_free(ctx);
      ctx = 0;

      PROCESS_END();
}
;
