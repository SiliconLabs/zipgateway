/* © 2014 Silicon Laboratories Inc.
 */
#include "contiki.h"
#include "Serialapi.h"
#include "security_layer.h"
#include "Security_Scheme0.h"
#include "ZW_SendDataAppl.h"
#include "ZW_PRNG.h"
#include "ZIP_Router_logging.h"
#include "zip_router_config.h"
#include "random.h"
#define NONCE_OPT 0

/**/
#define NONCE_TABLE_SIZE 5*3
#define NUM_TX_SESSIONS 2
#define MAX_ENCRYPTED_MSG_SIZE 128
#define MAX_NONCES 10
#define MAX_RXSESSIONS 2

#define NONCE_TIMEOUT 10
#define NONCE_REQUEST_TIMEOUT_MSEC 500

#define NONCE_BLACKLIST_SIZE 10
#define RECEIVERS_NONCE_SIZE 8    /* The size of the nonce field in a Nonce Report */

#define S0_KEY_SIZE 16  /* The S0 Key Size */

typedef enum {
  NONCE_GET,
  NONCE_GET_SENT,
  ENC_MSG,
  ENC_MSG_SENT,
  ENC_MSG2,
  ENC_MSG2_SENT,
  TX_DONE,
  TX_FAIL} tx_state_t;


typedef struct sec_tx_session {
  ts_param_t param;
  const uint8_t* data;            //Pointer the data to be encrypted
  uint8_t data_len;         //Length of the data to be encrypted
  tx_state_t state;
  uint8_t tx_code;
  clock_time_t transition_time;
  struct ctimer timer;      //Session timer
  uint8_t crypted_msg[46];
  uint8_t seq;              //Sequence number used in multisegment messages
  void*   user;             //User supplied pointer which is returned in the callback
  ZW_SendDataAppl_Callback_t callback;//Callback function, called when the session has been terminated
  TX_STATUS_TYPE tx_ext_status; // Extended TX status, most notably IMA information
} sec_tx_session_t;

typedef struct _AUTHDATA_ {
  uint8_t sh; /* Security Header for authentication */
  uint8_t senderNodeID; /* Sender ID for authentication */
  uint8_t receiverNodeID; /* Receiver ID for authentication */
  uint8_t payloadLength; /* Length of authenticated payload */
} auth_data_t;

extern u8_t send_data(ts_param_t* p, const u8_t* data, u16_t len,ZW_SendDataAppl_Callback_t cb,void* user);

static sec_tx_session_t tx_sessions[NUM_TX_SESSIONS];
uint8_t networkKey[S0_KEY_SIZE] = {0};                 /* The master key */


/* Nonce blacklist type*/
typedef struct {
  uint8_t nonce[RECEIVERS_NONCE_SIZE];
  uint8_t src;
  uint8_t dst;
  unsigned int in_use;
} nonce_blacklist_t;
nonce_blacklist_t nonce_blacklist[NONCE_BLACKLIST_SIZE];

static unsigned int blacklist_next_elem; /* Cyclic counter for the next element in nonce blacklist */

/************************ AES Helper functions ********************************************/
static uint8_t aes_key[16];
static uint8_t aes_iv[16];

void aes_encrypt(uint8_t *in, uint8_t* out) {
  SerialAPI_AES128_Encrypt(in,out,aes_key);
}

void aes_set_key_tpt(uint8_t* key,uint8_t* iv) {
  memcpy(aes_key,key,16);
  memcpy(aes_iv,iv,16);
}

void aes_ofb(uint8_t* data,uint8_t len) {
  uint8_t i;
  for (i = 0; i < len; i++) {
    if ((i & 0xF) == 0x0) {
      aes_encrypt(aes_iv, aes_iv);
    }
    data[i] ^= aes_iv[(i & 0xF)];
  }
}

/*
 * Caclucalte the authtag for the message,
 */
void aes_cbc_mac(uint8_t* data,uint8_t len,uint8_t* mac) {
  uint8_t i;

  aes_encrypt(aes_iv,mac);
  for (i = 0; i < len; i++) {
    mac[i & 0xF] ^= data[i];
    if ((i & 0xF) == 0xF) {
      aes_encrypt(mac, mac);
    }
  }

  /*if len is not divisible by 16 do the final pass, this is the padding described in the spec */
  if(len & 0xF) {
    aes_encrypt(mac, mac);
  }
}

/**
 * Generate 8 random bytes
 */
void aes_random8(uint8_t*d ) {
  PRNGOutput(d);
}

/******************************** Nonce Blacklist **********************************************/
/**
 * Test if an S0 nonce with particular source and destination is blacklisted.
 *
 * \return 1 if nonce is in blacklist, 0 otherwise
 */
static unsigned int sec0_is_nonce_blacklisted(const uint8_t src, const uint8_t dst, const uint8_t *nonce)
{
  for (unsigned int i = 0; i < NONCE_BLACKLIST_SIZE; i++)
  {
    if (nonce_blacklist[i].in_use
        && (0 == memcmp(nonce_blacklist[i].nonce, nonce, 8))
        && (nonce_blacklist[i].src == src)
        && (nonce_blacklist[i].dst == dst)) {
      return 1;
    }
  }
  return 0;
}

/**
 *  We blacklist the last NONCE_BLACKLIST_SIZE external nonces we have used.
 *  This function adds nonce to the blacklist.
 */
static void sec0_blacklist_add_nonce(const uint8_t src, const uint8_t dst, const uint8_t *nonce)
{
  nonce_blacklist[blacklist_next_elem].in_use = 1;
  memcpy(nonce_blacklist[blacklist_next_elem].nonce, nonce, RECEIVERS_NONCE_SIZE);
  nonce_blacklist[blacklist_next_elem].src = src;
  nonce_blacklist[blacklist_next_elem].dst = dst;
  blacklist_next_elem = (blacklist_next_elem + 1) % NONCE_BLACKLIST_SIZE;
}

/******************************** Nonce Management **********************************************/

typedef struct nonce {
  u8_t src;
  u8_t dst;
  u8_t timeout;
  u8_t reply_nonce; //indicate if this nonce from a enc message sent by me
  u8_t nonce[8];
} nonce_t;

static nonce_t nonce_table[NONCE_TABLE_SIZE]; //Nonces received or sent
static struct ctimer nonce_timer;

/**
 * Register a new nonce from sent from src to dst
 */
static u8_t register_nonce(u8_t src, u8_t dst,u8_t reply_nonce, const u8_t nonce[8]) {
  u8_t i;

  if(reply_nonce) {
    /*Only one reply nonce is allowed*/
    for (i=0; i< NONCE_TABLE_SIZE; i++) {
      if( nonce_table[i].reply_nonce &&
          nonce_table[i].timeout > 0 &&
          nonce_table[i].src == src &&
          nonce_table[i].dst == dst) {
        DBG_PRINTF("Reply nonce overwritten\n");
        memcpy(nonce_table[i].nonce,nonce,8);
        nonce_table[i].timeout = NONCE_TIMEOUT;
        return 1;
      }
    }
  }

  for (i=0; i< NONCE_TABLE_SIZE; i++) {
    if(nonce_table[i].timeout == 0) {
      nonce_table[i].src = src;
      nonce_table[i].dst = dst;
      nonce_table[i].reply_nonce = reply_nonce;
      memcpy(nonce_table[i].nonce,nonce,8);
      nonce_table[i].timeout = NONCE_TIMEOUT;
      return 1;
    }
  }

  ERR_PRINTF("Nonce table is full\n");
  return 0;
}

uint8_t has_three_nonces(u8_t src, u8_t dst)
{
  uint8_t nonce_count = 0;
  for (int i=0; i< NONCE_TABLE_SIZE; i++) {
    if (nonce_table[i].timeout > 0 && nonce_table[i].src == src &&
        nonce_table[i].dst == dst) {
      nonce_count++;
    }
  }
  if (nonce_count == 3) {
    return 1;
  }
  return 0;
}
/**
 * Receive nonce sent from src to dst, if th ri. If a nonce
 * is found the remove all entries from that src->dst combination
 * from the table.
 *
 * If any_nonce is set then ri is ignored
 */
static u8_t get_nonce(u8_t src, u8_t dst,u8_t ri, u8_t nonce[8],u8_t any_nonce) {
  u8_t i;
  u8_t rc;
  rc = 0;
  for (i=0; i< NONCE_TABLE_SIZE; i++) {
    if(nonce_table[i].timeout>0 && nonce_table[i].src == src && nonce_table[i].dst == dst) {
      if(any_nonce ||  nonce_table[i].nonce[0] == ri) {
        memcpy(nonce,nonce_table[i].nonce,8);
        return 1;
      }
    }
  }
  return 0;
}

/**
 * Remove all nonces from nonce table sent from src to dst
 * @param src
 * @param dst
 */
static void nonce_clear(u8_t src, u8_t dst)
{
  u8_t i;
  /*Remove entries from table from that source dest combination */
  for (i=0; i< NONCE_TABLE_SIZE; i++) {
    if(nonce_table[i].timeout && nonce_table[i].src == src && nonce_table[i].dst == dst) {
      nonce_table[i].timeout = 0;
    }
  }
}

static void nonce_timer_timeout(void* data) {
  u8_t i;
  for (i=0; i< NONCE_TABLE_SIZE; i++) {
    if(nonce_table[i].timeout > 0) {
      nonce_table[i].timeout--;
    }
  }
  ctimer_set(&nonce_timer,1000,nonce_timer_timeout,0);
}

/********************************Security TX Code ***************************************************************/
static void tx_session_state_set(sec_tx_session_t* s,tx_state_t state);
static uint8_t enckey[16];
static uint8_t authkey[16];
static uint8_t enckeyz[16];
static uint8_t authkeyz[16];

/**
 * Set the network key
 */
void sec0_set_key(const uint8_t* netkey)REENTRANT {
  uint8_t p[16];
  uint8_t temp[16] ={ 0};

  if (memcmp(netkey, temp, 16) == 0) {
    WRN_PRINTF("sec0_set_key with all zero key\n");
  }else{
    WRN_PRINTF("sec0_set_key \n");
  }

  memcpy(aes_key,netkey,16);
  memset(p,0x55,16);
  aes_encrypt(p,authkey);
  memset(p,0xAA,16);
  aes_encrypt(p,enckey);

  memcpy(aes_key,temp,16);
  memset(p,0x55,16);
  aes_encrypt(p,authkeyz);
  memset(p,0xAA,16);
  aes_encrypt(p,enckeyz);


}


static void tx_timeout(void* user) {
  sec_tx_session_t* s = (sec_tx_session_t*) user;
  ERR_PRINTF("Security0 transmit timeout %p\n", s);
  s->tx_code = TRANSMIT_COMPLETE_FAIL;
  tx_session_state_set(s,TX_FAIL);
}

/**
 * Lookup a tx session by nodeid
 */
static sec_tx_session_t* get_tx_session_by_node(uint8_t snode, uint8_t dnode) {
  uint8_t i;
  for(i=0; i < NUM_TX_SESSIONS;i++) {
    if(tx_sessions[i].param.dnode == dnode && tx_sessions[i].param.snode == snode) {
      return &tx_sessions[i];
    }
  }
  return 0;
}

/**
 * Get the maximum frame size supported by a node.
 */
static uint8_t get_node_max_frame_size(u8_t dnode) {
  /*FIXME This should be made more clever*/
  return 46;
}

/**
 * Encrypt a message and write the encrypted data into s->crypted_msg
 */
static uint8_t encrypt_msg(sec_tx_session_t* s,uint8_t pass2) REENTRANT{
  uint8_t iv[16],tmp[8]; /* Initialization vector for enc, dec,& auth */
  uint8_t mac[16];
  uint8_t len; // Length of the encrypted part
  uint8_t more_to_send;
  uint8_t* enc_data;
  auth_data_t* auth;
  uint8_t maxlen = get_node_max_frame_size(s->param.dnode);

  len = s->data_len;
  more_to_send = 0;

  /*Check if we should break this message in two */
  if(len + 20 > maxlen) {
    len = maxlen-20;
    more_to_send = 1;
  }

  ASSERT(len + 20 <= sizeof(s->crypted_msg));
  /* Make the IV */

  do {
    aes_random8(iv);
  } while( get_nonce(s->param.dnode,s->param.snode,iv[0],tmp,FALSE) );

  /*Choose a nonce from sender */
  if(!get_nonce(s->param.dnode,s->param.snode,0,iv+8,TRUE)) {
    ERR_PRINTF("Nonce for node %u --> %u is not found\n",s->param.dnode,s->param.snode);
    return 0;
  } else {
    DBG_PRINTF("Clearing nonce for %u --> %u\n",s->param.dnode,s->param.snode);
    nonce_clear(s->param.dnode,s->param.snode);
  }

  /*Register my nonce */
  register_nonce(s->param.snode,s->param.dnode,TRUE,iv);


  /* Setup pointers */
  enc_data = s->crypted_msg + 10;
  auth = (auth_data_t* )(s->crypted_msg + 6);

  /* Copy data into a second buffer Insert security flags */

  if(pass2) {
    *enc_data = SECURITY_MESSAGE_ENCAPSULATION_PROPERTIES1_SEQUENCED_BIT_MASK | SECURITY_MESSAGE_ENCAPSULATION_PROPERTIES1_SECOND_FRAME_BIT_MASK | (s->seq & 0xF);
  } else if(more_to_send) {
    *enc_data = SECURITY_MESSAGE_ENCAPSULATION_PROPERTIES1_SEQUENCED_BIT_MASK  | (s->seq & 0xF);
  } else {
    *enc_data = 0;
  }

  memcpy(enc_data+1, s->data, len);


  if ((s->data[0] == COMMAND_CLASS_SECURITY) && (s->data[1] == NETWORK_KEY_SET)) {
    DBG_PRINTF("COMMAND_CLASS_SECURITY, NETWORK_KEY_SET\n");
   /*Encrypt */
    aes_set_key_tpt(enckeyz,iv);
  } else {
   /*Encrypt */
    aes_set_key_tpt(enckey,iv);
  }

  aes_ofb(enc_data,len+1);

  /*Fill in the auth structure*/
  auth->sh = more_to_send ? SECURITY_MESSAGE_ENCAPSULATION_NONCE_GET : SECURITY_MESSAGE_ENCAPSULATION;
  auth->senderNodeID = s->param.snode;
  auth->receiverNodeID = s->param.dnode;
  auth->payloadLength = len + 1;

  if ((s->data[0] == COMMAND_CLASS_SECURITY) && (s->data[1] == NETWORK_KEY_SET)) {
    DBG_PRINTF("COMMAND_CLASS_SECURITY, NETWORK_KEY_SET\n");
    /* Authtag */
    aes_set_key_tpt(authkeyz,iv);
  } else {
    /* Authtag */
    aes_set_key_tpt(authkey,iv);
  }
  aes_cbc_mac((uint8_t*)auth,4 + len+1,mac);
  s->crypted_msg[0] = COMMAND_CLASS_SECURITY;
  s->crypted_msg[1] = auth->sh;
  memcpy(s->crypted_msg+2, iv,8);

  s->crypted_msg[2 +8 + len + 1] = iv[8];
  memcpy(s->crypted_msg+2 + 8 + len + 2,mac,8);

  s->data += len;
  s->data_len -=len;
  return len + 20;
}

static void noce_get_callback(uint8_t status,void* user, TX_STATUS_TYPE *t) {
  sec_tx_session_t* s = (sec_tx_session_t*) user;

  //ASSERT(s->state == NONCE_GET);

  s->tx_code = status;
  if(s->state == NONCE_GET)
    tx_session_state_set(s, status == TRANSMIT_COMPLETE_OK ? NONCE_GET_SENT : TX_FAIL);
}

static void msg1_callback(uint8_t status,void* user, TX_STATUS_TYPE *t) {
  sec_tx_session_t* s = (sec_tx_session_t*) user;

  //ASSERT(s->state == ENC_MSG);
  if(s->state == ENC_MSG) {
    s->tx_code = status;
    // TX ext status return from serial API only if transmit succeed
    if((status == TRANSMIT_COMPLETE_OK) && t) {
      s->tx_ext_status = *t;
    }
    tx_session_state_set(s, status == TRANSMIT_COMPLETE_OK ? ENC_MSG_SENT : TX_FAIL);
  }
}

static void msg2_callback(uint8_t status,void* user, TX_STATUS_TYPE *t) {
  sec_tx_session_t* s = (sec_tx_session_t*) user;

  //ASSERT(s->state == ENC_MSG2);
  if(s->state == ENC_MSG2) {
    s->tx_code = status;
    // TX ext status return from serial API only if transmit succeed
    if((status == TRANSMIT_COMPLETE_OK) && t) {
      s->tx_ext_status = *t;
    }
    tx_session_state_set(s, status == TRANSMIT_COMPLETE_OK ? ENC_MSG2_SENT : TX_FAIL);
  }
}

static void tx_session_state_set(sec_tx_session_t* s,tx_state_t state) {
  uint8_t len;
#if NONCE_OPT
  u8_t dummy[8];
#endif

  static const uint8_t nonce_get[] =  { COMMAND_CLASS_SECURITY, SECURITY_NONCE_GET };

  s->state = state;	  
  s->transition_time = clock_time();
  DBG_PRINTF("tx_session_state_set session: %p State %d\n", s, state);

  switch(s->state) {
  case NONCE_GET:
#if NONCE_OPT
    if(get_nonce(s->dnode.nodeid,s->snode.nodeid,0,dummy)) {
      tx_session_state_set(s,ENC_MSG);
      return;
    }
#endif
    if(!send_data(&s->param,nonce_get,sizeof(nonce_get), noce_get_callback,s) ) {
      s->tx_code = TRANSMIT_COMPLETE_FAIL;
      tx_session_state_set(s,TX_FAIL);
    } else if(secure_learn_active()) {
        ctimer_set(&s->timer,10000,tx_timeout,s);
    }
    break;
  case NONCE_GET_SENT:
    if(!secure_learn_active()) {
        ctimer_set(&s->timer, NONCE_REQUEST_TIMEOUT_MSEC + clock_time()-s->transition_time,tx_timeout,s);
    }
    break;
  case ENC_MSG:
    ctimer_stop(&s->timer);
    len = encrypt_msg(s,0);
    if(len==0 || !send_data(&s->param,s->crypted_msg,len, msg1_callback,s) ) {
      s->tx_code = TRANSMIT_COMPLETE_FAIL;
      tx_session_state_set(s,TX_FAIL);
    }
    break;
  case ENC_MSG_SENT:
    /*If there is no more data to send*/
    if(s->data_len==0) {
      tx_session_state_set(s,TX_DONE);
    } else {
      ctimer_set(&s->timer,NONCE_REQUEST_TIMEOUT_MSEC + clock_time()-s->transition_time,tx_timeout,s);
    }
    break;
  case ENC_MSG2:
    ctimer_stop(&s->timer);
    len = encrypt_msg(s,1);
    if(len==0 || !send_data(&s->param,s->crypted_msg,len, msg2_callback,s) ) {
      s->tx_code = TRANSMIT_COMPLETE_FAIL;
      tx_session_state_set(s,TX_FAIL);
    }
    break;
  case ENC_MSG2_SENT:
    tx_session_state_set(s,TX_DONE);
    break;
  case TX_DONE:
    s->param.dnode = 0;
    ctimer_stop(&s->timer);
    if(s->callback) s->callback(TRANSMIT_COMPLETE_OK,s->user, &s->tx_ext_status);
    memset(&s->tx_ext_status, 0, sizeof(s->tx_ext_status));
    break;
  case TX_FAIL:
    s->param.dnode= 0;
    ctimer_stop(&s->timer);
    if(s->callback) s->callback(s->tx_code,s->user, NULL);
    memset(&s->tx_ext_status, 0, sizeof(s->tx_ext_status));
    break;
  }
}

/**
 * Get the next sequence number no node may receive the same sequence number in two concurrent transmissions
 */
static uint8_t get_seq(uint8_t src, uint8_t dst) {
  static uint8_t s=0;
  s++; //FIXME make this routine do the right thing.
  return s;
}

uint8_t sec0_send_data(ts_param_t* p,const void* data, uint8_t len,ZW_SendDataAppl_Callback_t callback,void* user) {
  sec_tx_session_t* s;
  uint8_t i;

  s = get_tx_session_by_node(p->snode,p->dnode);
  if(s) {
    ERR_PRINTF("Already have one tx session from node %d to %d\n", p->snode,p->dnode);
    return FALSE;
  }

  for(i=0; i < NUM_TX_SESSIONS;i++) {
    if(tx_sessions[i].param.dnode == 0) {
      s = &tx_sessions[i];
      break;
    }
  }

  if(!s) {
    ERR_PRINTF("No more security TX sessions available\n");
    return FALSE;
  }

  s->param = *p;
  s->data = data;
  s->data_len = len;
  s->callback = callback;
  s->user = user;
  s->seq = get_seq(s->param.snode,s->param.dnode);

  DBG_PRINTF("New sessions for src %d dst %d\n",s->param.snode,s->param.dnode);
  tx_session_state_set(s,NONCE_GET);
  return TRUE;
}

/**
 * Register a nonce by source and destination
 */
void sec0_register_nonce(uint8_t src, uint8_t dst, const uint8_t* nonce) {
  sec_tx_session_t* s;
  if (sec0_is_nonce_blacklisted(src, dst, nonce)) {
    WRN_PRINTF("Ignoring duplicate nonce src %d dst %d\n", src, dst);
    return;
  }
  s = get_tx_session_by_node(dst,src);
  if(s) {
    register_nonce(src,dst,FALSE,nonce);
    sec0_blacklist_add_nonce(src, dst, nonce);
    if( s->state == NONCE_GET_SENT || s->state == NONCE_GET ) {
      //memcpy(s->nonce,nonce,8);
      tx_session_state_set(s,ENC_MSG);
    } else if (s->state == ENC_MSG_SENT  || (s->state == ENC_MSG && s->data_len>0)){
      //memcpy(s->nonce,nonce,8);
      tx_session_state_set(s,ENC_MSG2);
    }
  } else {
    WRN_PRINTF("Nonce report but not for me src %d dst %d\n",src,dst);
  }
}

/******************************** Security RX code ***********************************/


/*
 *
 * Jeg kunne lave en ordenlig rx session hvis man m���tte rapotere samme nonce flere gange
 * hvis den ikke var brugt.
 *
 * Jeg sender NONCE_GET, men jeg misser en ack s��� den bliver sendt flere gange
 * Jeg modtager derfor flere reports. Kunne disse reports ikke indholde samme nonce?
 * Jeg kan ikke se problemet, jeg koder
 *
 */
typedef enum {
  RX_INIT,
  RX_ENC1,
  RX_ENC2,
  RX_SESSION_DONE
} rx_session_state_t;


typedef struct {
  uint8_t snode;
  uint8_t dnode;
  rx_session_state_t state;
  uint8_t seq_nr;
  uint8_t msg[MAX_ENCRYPTED_MSG_SIZE];
  uint8_t msg_len;
  clock_time_t timeout;
} rx_session_t;



uint8_t is_free(rx_session_t* e) {
  return  (e->state == RX_SESSION_DONE) || (e->timeout < clock_time()) ;
}
rx_session_t rxsessions[MAX_RXSESSIONS];

/**
 * Get a new free RX session.
 */
rx_session_t* new_rx_session(uint8_t snode,uint8_t dnode) {
  uint8_t i;
  for(i=0; i < MAX_RXSESSIONS; i++) {
    if( is_free(&rxsessions[i])) {
      rxsessions[i].snode=snode;
      rxsessions[i].dnode=dnode;
      rxsessions[i].timeout = clock_time() + CLOCK_SECOND*10; //Timeout in 10s
      return &rxsessions[i];
    }
  }
  return 0;
}

void free_rx_session(rx_session_t* s) {
  //memset(s->nonce,0,sizeof(s->nonce));
  s->state = RX_SESSION_DONE;
}

void sec0_init() {
  DBG_PRINTF("Initializing S0\n");
  uint8_t i;

  for(i=0; i < MAX_RXSESSIONS; i++) {
    free_rx_session(&rxsessions[i]);
  }
  for(i=0; i < NUM_TX_SESSIONS; i++) {
    ctimer_stop(&tx_sessions[i].timer);
    memset(&tx_sessions[i],0,sizeof(sec_tx_session_t));
  }

  for (i=0; i<NONCE_BLACKLIST_SIZE; i++) {
    memset(&nonce_blacklist[i], 0, sizeof(nonce_blacklist_t));
  }
  blacklist_next_elem = 0;

  /* We do not re-initialize the nonce table here, bacause it is
   * actually desirable to keep the nonces after a gateway reset. The nonces will
   * timeout anyway. This fixes a situation like TO#05875, where the gateway get a nonce
   * get just before it resets after learnmode has completed.
   */
  /*for(i =0; i < NONCE_TABLE_SIZE; i++) {
    nonce_table[i].timeout = 0;
  }*/
  ctimer_stop(&nonce_timer);
  ctimer_set(&nonce_timer,1000,nonce_timer_timeout,0);
}

void sec0_abort_all_tx_sessions() {
  u8_t i;
  DBG_PRINTF("Aborting all S0 TX sessions\n");
  for(i=0; i < NUM_TX_SESSIONS; i++) {
    if(tx_sessions[i].param.dnode) {
      tx_sessions[i].tx_code = TRANSMIT_COMPLETE_FAIL;
      tx_session_state_set(&tx_sessions[i],TX_FAIL);
    }
  }
}

/**
 * Get a specific nonce from the nonce table. The session must not be expired
 */
rx_session_t* get_rx_session_by_nodes(uint8_t snode, uint8_t dnode) {
  uint8_t i;
  rx_session_t* e;
  for(i=0; i < MAX_RXSESSIONS; i++) {
    e = &rxsessions[i];
    if( !is_free(e) &&
        e->dnode == dnode &&
        e->snode == snode) {
      return e;
    }
  }
  return 0;
}

/**
 * Send a nonce from given source to given destination. The nonce is registered internally
 */
void sec0_send_nonce(ts_param_t *src)REENTRANT {
  u8_t nonce[8],tmp[8];
  ts_param_t dst;
  static ZW_SECURITY_NONCE_REPORT_FRAME nonce_res;

  /* ZGW-3259, Lot of routed retransmitted Sec0 nonce gets get the nonce table
   * full when GW sends nonce reports for all of them and jam the S0 network 
   * from Gateway */
  if (has_three_nonces(src->dnode, src->snode)) {
    WRN_PRINTF("There are three nonces for %d->%d in table. Discarding further "
               "nonce get "
               "requests coming from %d\n",
               src->dnode, src->snode, src->snode);
    return;
  }

  do {
    aes_random8(nonce);
  } while( get_nonce(src->dnode,src->snode,nonce[0],tmp,FALSE) );

  nonce_res.cmdClass = COMMAND_CLASS_SECURITY;
  nonce_res.cmd = SECURITY_NONCE_REPORT;
  memcpy(&nonce_res.nonceByte1,nonce,8);

  /*Swap source and destination*/
  ts_param_make_reply(&dst, src);

  if( send_data(&dst,(u8_t*)&nonce_res,sizeof(nonce_res), 0,0)) {
    register_nonce(src->dnode,src->snode,FALSE,nonce);
  }
}

uint8_t sec0_decrypt_message(uint8_t snode, uint8_t dnode, uint8_t* enc_data, uint8_t enc_data_length,uint8_t* dec_message) REENTRANT {
    uint8_t iv[16]; /* Initialization vector for enc, dec,& auth */
    uint8_t mac[16];
    rx_session_t *s;
    uint8_t *enc_payload;
    uint8_t ri;
    // Allocate a buffer that's large enough for the largest frame size that's valid
    // Maximum valid size is MAX_ENCRYPTED_MSG_SIZE + 20 bytes overhead + auth header size
    uint8_t auth_buff[MAX_ENCRYPTED_MSG_SIZE + 20 + sizeof(auth_data_t)];
    auth_data_t* auth;
    uint8_t flags;

    // Check correct lower bound of data length
    if(enc_data_length < 20) {
      ERR_PRINTF("Encrypted message is too short\n");
      return 0;
    }

    // Detect if message is too long to decrypt
    if((enc_data_length-20) > MAX_ENCRYPTED_MSG_SIZE) {
      ERR_PRINTF("Encrypted message is too long\n");
      return 0;
    }

    ri =  enc_data[enc_data_length-9];

    /*Build the IV*/
    memcpy(iv,enc_data+2,8);

    /*Find the nonce in the nonce table */
    if(!get_nonce(dnode,snode,ri,&iv[8],FALSE)) {
      WRN_PRINTF("Nonce for %d -> %d not found\n",(int)dnode, (int)snode);
      return 0;
    } else {
      DBG_PRINTF("Clearing nonce for %d -> %d\n",(int)dnode, (int)snode);
      nonce_clear(dnode,snode);
    }

#if NONCE_OPT
    register_nonce(snode,dnode,TRUE,enc_data+2);
#endif

    /*TODO don't create sessions for single fragment frames*/
    s = get_rx_session_by_nodes(snode,dnode);
    if(!s)
    {
      s = new_rx_session(snode,dnode);
      if(s) {
        s->state = RX_INIT;
      } else {
        WRN_PRINTF("no more RX sessions available\n");
        return 0;
      }
    }

    // When we get a session that's in progress, verify the size of the data we have and the data we're about
    // to add do not go over our total output buffer size. If it does, drop the frame and the session pool will free up the
    // invalid session in a little bit.
    if(s->state != RX_INIT && (s->msg_len + enc_data_length - 20) > MAX_ENCRYPTED_MSG_SIZE) {
      ERR_PRINTF("Combined data for encrypted message is too long\n");
      return 0;
    }

    enc_payload = enc_data+2+8;

    // Use a stack buffer instead of dec_message for auth verification
    auth = (auth_data_t*)auth_buff;
    /*Fill in the auth structure*/
    auth->sh = enc_data[1];
    auth->senderNodeID = snode;
    auth->receiverNodeID = dnode;
    auth->payloadLength = enc_data_length - 19;
    memcpy( (uint8_t*)auth+4, enc_payload, auth->payloadLength);

    /* Authtag */
    aes_set_key_tpt(authkey,iv);
    aes_cbc_mac((uint8_t*)auth,4 + auth->payloadLength,mac);

    if(memcmp(mac, enc_data+ enc_data_length-8,8) !=0 ) {
      ERR_PRINTF("Unable to verify auth tag\n");
      return 0;
    }
    DBG_PRINTF("Authentication verified\n");
    /*Decrypt */
    aes_set_key_tpt(enckey,iv);
    aes_ofb(enc_payload,auth->payloadLength);

    flags = *enc_payload;

    if(flags & SECURITY_MESSAGE_ENCAPSULATION_PROPERTIES1_SEQUENCED_BIT_MASK ) {
      if((flags & SECURITY_MESSAGE_ENCAPSULATION_PROPERTIES1_SECOND_FRAME_BIT_MASK)==0) {
        //First frame
        s->seq_nr = flags & 0xF;
#ifndef __ASIX_C51__
        ERR_PRINTF("State is %d seq %u expecetd %u\n", (int)s->state,flags & 0xF,s->seq_nr);
#endif
        s->msg_len = enc_data_length - 20;
        s->state=RX_ENC1;
        memcpy(s->msg, enc_payload+1,s->msg_len);
        return 0;
      } else {
        //Second frame
        if((s->state != RX_ENC1) || (flags & 0xF) != s->seq_nr) {
          ERR_PRINTF("State is %d seq %u expecetd %u\n", (int)s->state,flags & 0xF,s->seq_nr);
          goto state_error;
        } else {
          s->state  = RX_ENC2;
        }
        memcpy(dec_message,s->msg, s->msg_len);
        memcpy(dec_message + s->msg_len,enc_payload+1 ,enc_data_length - 20);

        free_rx_session(s);
        return (s->msg_len + enc_data_length - 20);
      }
    } else {
      /* Single frame message */
      memcpy(dec_message,enc_payload+1 ,enc_data_length - 20);
      free_rx_session(s);
      return (enc_data_length - 20);
    }
    return 0;
state_error:
  ERR_PRINTF("Security RX session is not in the right state\n");
  return 0;
}


void sec0_reset_netkey(void) {
  uint8_t n=0;
  bool bSuccess=false;

  LOG_PRINTF("Reinitializing S0 network key (S2 keys are unchanged)\n");
  do {
    bSuccess = dev_urandom(sizeof(networkKey),networkKey);
    if (bSuccess) {
      break;
    }
  } while (n++ <= 10); 

  if(bSuccess) {
    nvm_config_set(security_netkey,networkKey);
  }
  else {
    ERR_PRINTF("Failed to generate random S0 key. Security compromised!\n");
  }

  ASSERT(bSuccess);
}
