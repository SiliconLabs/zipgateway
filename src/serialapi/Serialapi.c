/* © 2014 Silicon Laboratories Inc.
 */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/

#ifdef _MSC_VER
//For Sleep call
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "TYPES.H"      // Standard types
#include "ZIP_Router_logging.h"

#include <string.h>
#include <ZW_basis_api.h>
#include <ZW_SerialAPI.h>
#include <ZW_controller_api.h>
#include <ZW_controller_static_api.h>
#include <ZW_mem_api.h>
#include "port.h"
#include "ZW_classcmd_ex.h"

#include "serial_api_process.h"
#include "conhandle.h"

#include "Serialapi.h"
#include <string.h>
#include <stdlib.h>
#include <ZIP_Router_logging.h>

#ifdef __ROUTER_VERSION__
#include "net/uip.h" //TODO: seems to be needed somewhere, figure out if it can't be removed
#endif

#define NEW_NODEINFO

#define MAX_RXQUEUE_LEN 10
#define INVALID_TIMER_HANDLE 255
#define LR_NOT_SUPPORTED 128

#ifdef __ASIX_C51__
#define SER_PRINTF printf
#define ERR_PRINTF printf
#else
#define SER_PRINTF(f, ...) printf("\033[36;1m SerialAPI: " f "\033[0m",  ##__VA_ARGS__ )
#endif



extern void TimerAction( void );


/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/

typedef struct SerialAPICpabilities {
  BYTE appl_version;
  BYTE appl_revision;
  BYTE manufactor_id1;
  BYTE manufactor_id2;
  BYTE product_type1;
  BYTE product_type2;
  BYTE product_id1;
  BYTE product_id2;
  BYTE supported_bitmask[29];
  BYTE supported_serialapi_bitmask[29];
  BYTE nodelist[29];
  BYTE chipType;
  BYTE chipVersion;
} SerialAPICpabilities_t;
SerialAPICpabilities_t capabilities;

typedef struct chip_data {
   uint8_t chip_type;
   uint8_t chip_version;
} chip_data_t;
chip_data_t my_chip_data;


#undef ASSERT
#ifdef SERIALAPI_DEBUG
#define ASSERT(a) {if(!(a)) __asm("int $3");}
#else
#define ASSERT(a) {if(!(a)) SER_PRINTF("Assertion failed at %s:%i\n", __FILE__,__LINE__ );}
#endif


#define SupportsCommand(cmd) ((cmd==FUNC_ID_SERIAL_API_GET_CAPABILITIES) || (capabilities.supported_bitmask[((cmd-1)>>3)]  & (1<<((cmd-1) & 0x7))))

void SerialAPI_ApplicationNodeInformation( BYTE listening, APPL_NODE_TYPE nodeType, BYTE *nodeParm, BYTE parmLength );
static node_id_type_t SerialAPI_Setup_NodeID_BaseType_Set(node_id_type_t nodeid_basetype);
static void SerialAPI_Enable_LR_Virtual_Nodes();
static void Dispatch( BYTE *pData , uint16_t len);
static int SendFrameWithResponse(BYTE cmd, BYTE *Buf, BYTE len,BYTE *reply, BYTE *replyLen );

LEARN_INFO learnNodeInfo;

#ifdef __ASIX_C51__
extern uint8_t gIsSslHshakeInProgress;
#endif

BYTE buffer[ BUF_SIZE ]; /* Serial API tx buffer */
BYTE pCmd[ BUF_SIZE ];   /* Serial API rx buffer */

struct list{
  void* __data;
  struct list* next;
  BYTE len;
};
static struct list* rxQueue;


static BYTE idx;
static BYTE byLen;
static BYTE byCompletedFunc;
static struct SerialAPI_Callbacks* callbacks;
static int lr_enabled = 0;
static void set_node_id_in_buffer(uint16_t node_id)
{
    if (lr_enabled) {
      buffer[ idx++ ] = node_id >> 8;
    }
    buffer[ idx++ ] = node_id & 0xff;
}
/**
  * \ingroup SerialAPI
  * Max number of retransmissions/retries.
  */
#define MAX_SERIAL_RETRY 3
#define TIMEOUT_TIME 1600
/**
  * \ingroup SerialAPI
  * \defgroup SAUSC UART Status Codes
  * @{
  */
#define STATUS_RXTIMEOUT 1
#define STATUS_FRAMESENT 2
#define STATUS_FRAMEERROR 3
#define STATUS_FRAMERECEIVED 4
/** @} */

#define IDX_CMD   2
#define IDX_DATA  3
#define data __data

/**
  * \ingroup SerialAPI
  * \defgroup SACB Callbacks
  * @{ZW_APPLICATION_TX_BUFFER
  */
static VOID_CALLBACKFUNC(cbFuncZWSendData)(BYTE, TX_STATUS_TYPE*);
static VOID_CALLBACKFUNC(cbFuncZWSendTestFrame)(BYTE);
static VOID_CALLBACKFUNC(cbFuncZWSendDataBridge)(BYTE, TX_STATUS_TYPE*);
static VOID_CALLBACKFUNC(cbFuncZWSendDataMultiBridge)(BYTE);
static void ( *cbFuncZWSendNodeInformation ) ( BYTE txStatus );
static void ( *cbFuncMemoryPutBuffer ) ( void );
static void ( *cbFuncZWSetDefault ) ( void );
static void ( *cbFuncZWNewController ) ( LEARN_INFO* );
static void ( *cbFuncRemoveNodeFromNetwork ) ( LEARN_INFO* );
static void ( *cbFuncAddNodeToNetwork ) ( LEARN_INFO* );
static void ( *cbFuncZWControllerChange ) ( LEARN_INFO* );

static void ( *cbFuncZWReplicationSendData ) ( BYTE txStatus );
static void ( *cbFuncZWAssignReturnRoute ) ( BYTE bStatus );
static void ( *cbFuncZWAssignSUCReturnRoute ) ( BYTE bStatus );
static void ( *cbFuncZWDeleteSUCReturnRoute ) ( BYTE bStatus );
static void ( *cbFuncZWDeleteReturnRoute ) ( BYTE bStatus );
static void ( *cbFuncZWSetLearnMode ) (LEARN_INFO*);
static void ( *cbFuncZWSetSlaveLearnMode ) (BYTE bStatus, uint16_t orgID, uint16_t newID);
static void ( *cbFuncZWRequestNodeNodeNeighborUpdate ) ( BYTE bStatus );
static void ( *cbFuncZWSetSUCNodeID ) ( BYTE bSstatus );
static void ( *cbFuncZWRequestNetworkUpdate ) ( BYTE txStatus );
static void ( *cbFuncZWRemoveFailedNode ) ( BYTE txStatus );
static void ( *cbFuncZWReplaceFailedNode ) ( BYTE txStatus );
static VOID_CALLBACKFUNC(cbFuncZWSendSUCID)(BYTE, TX_STATUS_TYPE*);
/** @} */

static bool is_nodeid_basetype_8();
const char* zw_lib_names[] = {
    "Unknown",
    "Static controller",
    "Bridge controller library",
    "Portable controller library",
    "Enhanced slave library",
    "Slave library",
    "Installer library",
};

#ifdef __ROUTER_VERSION__
struct ZW_timer {
/*  PORT_TIMER timer;*/
  struct ctimer timer;
  BYTE repeats;
  void (*f)();
};
#endif

/*
static void dump_serbuf()  {
  int i;

  SER_PRINTF("------------Serial Buffer --------------\n");
  for(i=0; i < serBufLen; i++) {
    printf("%02x ", serBuf[i]);
  }
  printf("\n");
  SER_PRINTF("------------------------------------\n");
}*/

/***************************** Timers ***********************************/
#ifdef __ROUTER_VERSION__
static struct ZW_timer timers[0x10];
#endif

uint8_t SerialAPI_SupportsCommand_func(uint8_t func_id)
{
  return (SupportsCommand(func_id)) > 0;
}

static bool is_bit_num_set_in_byte(uint8_t bit_num, uint8_t byte)
{
  if (byte & (1 << bit_num)) {
    return true;
  } else {
    return false;
  }
}

BYTE SupportsSerialAPISetup_func(BYTE subcmd)
{ 
  // check if the subcmd being checked has two bits set for e.g. 3(0011), 5(0101)
  // Where we need to check the Extended Z-Wave API Setup Supported Sub Commands bitmask
  if (subcmd & (subcmd - 1)) {
    if (subcmd > 16) {
      ERR_PRINTF("Error: Checking if 0x%02X is supported, requires checking "
                   "beyond 1st byte of Extended Z-Wave API Setup Supported Sub"
                   "Commands bitmask, which is not supported.\n", subcmd);
    } else if (subcmd > 8) {
      return (is_bit_num_set_in_byte(subcmd - 8, capabilities.supported_serialapi_bitmask[2]));
    } else if (subcmd > 0) {
      return (is_bit_num_set_in_byte(subcmd ,capabilities.supported_serialapi_bitmask[1]));
    }
  } else { // Check the bitflag as only one bit is set
    return (capabilities.supported_serialapi_bitmask[0] & subcmd);
  }
  return 0;
}

bool SerialAPI_SupportsLR() {
  return SupportsSerialAPISetup_func(SERIAL_API_SETUP_CMD_NODEID_BASETYPE_SET);
}

#ifdef __ROUTER_VERSION__
BYTE TimerStart(void (*func)(),BYTE timerTicks, BYTE repeats) {
  return ZW_LTimerStart(func,timerTicks,repeats);
}

BYTE TimerRestart(BYTE handle) {
  return ZW_LTimerRestart(handle);
}

BYTE TimerCancel(BYTE handle) {
  return ZW_LTimerCancel(handle);
}


void timer_timeout(void* p) {
  struct ZW_timer *t = (struct ZW_timer *)p;

  if(t->f) {
    t->f();
  }
  if(t->repeats > 0 && t->repeats != TIMER_FOREVER) {
    t->repeats--;
  }

  if(t->repeats==0) {
    t->f=0;
  } else {
    ctimer_restart(&t->timer);
  }
}


/**
 * Create a new timer.
 *
 * @param[in] func         Callback to be called when timer expires.
 * @param[in] timerTicks   Timer interval in units of 10ms
 * @param[in] repeats      Number of times the timer fires. 0xFF makes the timer fire forever
 * \retval a handle to the timer just created.
 */
BYTE ZW_LTimerStart(void (*func)(),unsigned long timerTicks, BYTE repeats) {
  uint8_t i;
  for(i=0; i < sizeof(timers)/(sizeof(struct ZW_timer)); i++) {
    if(timers[i].f==NULL) {
      timers[i].f = func;
      timers[i].repeats = repeats;
      ctimer_set(&timers[i].timer,timerTicks*10,timer_timeout,&timers[i]);
      return i;
    }
  }
  ASSERT(0);
  return 0xFF;
}


/**
 * Restart the timer
 * @param[in] handle Handle of timer to restart
 */
BYTE ZW_LTimerRestart(BYTE handle) {
  if(handle <  (sizeof(timers)/(sizeof(struct ZW_timer))) ) {
    ctimer_reset(&timers[handle].timer);
    return TRUE;
  } else {
    return FALSE;
  }
}

/**
 * Cancel the timer
 * @param[in] handle Handle of timer to cancel
 */
BYTE ZW_LTimerCancel(BYTE handle) {
  if(handle <  (sizeof(timers)/(sizeof(struct ZW_timer))) ) {
    ctimer_stop(&timers[handle].timer);
    timers[handle].f = 0;
    return TRUE;
  } else {
    return FALSE;
  }
}
#endif

/**********************************************************/
/**
 * \ingroup BASIS
 * \macro{ZW_RANDOM()}
 *
 * A pseudo-random number generator that generates a sequence of numbers,
 * the elements of which are approximately independent of each other.
 * The same sequence of pseudo-random numbers will be repeated in
 * case the module is power cycled. The Z-Wave protocol uses also this
 * function in the random backoff algorithm etc.
 *
 * \return
 * Random number (0 – 0xFF)
 *
 *
 * \serialapi{
 * HOST->ZW: REQ | 0x1D
 * ZW->HOST: RES | 0x1D | rndNo
 * }
 */
BYTE ZW_Random() {
  return PORT_RANDOM() & 0xFF;
}
/**
  * \ingroup SerialAPI
  * Initialize callbacks and transmission variables.

  * \param[in] serial_port Name of the serial port to use, on Linux ie. "/dev/ttyS1"
  * \param[in] _callbacks Callback to be used by the serial API.
  * \return = TRUE if initialization was succesful
  */
BOOL SerialAPI_Init(const char* serial_port, const struct SerialAPI_Callbacks* _callbacks )
{
  BYTE ver, cap, len, chip_type, chip_version;
  BYTE nodelist[32];
  BYTE buf[14];
  BYTE listening;
  APPL_NODE_TYPE nodeType;
  BYTE *nodeParm;
  BYTE parmLength;
  BYTE type;
  int i;
    if(!ConInit(serial_port)) return FALSE;


    cbFuncZWSendData = NULL;
    cbFuncZWSendTestFrame = NULL;
    cbFuncZWSendDataBridge = NULL;
    cbFuncZWSendDataMultiBridge = NULL;
    cbFuncZWSendNodeInformation = NULL;
    cbFuncMemoryPutBuffer = NULL;
    cbFuncZWSetDefault = NULL;
    cbFuncZWNewController = NULL;
    cbFuncAddNodeToNetwork = NULL;
    cbFuncRemoveNodeFromNetwork = NULL;
    cbFuncZWControllerChange = NULL;
    cbFuncZWReplicationSendData = NULL;
    cbFuncZWAssignReturnRoute = NULL;
    cbFuncZWAssignSUCReturnRoute = NULL;
    cbFuncZWDeleteSUCReturnRoute = NULL;
    cbFuncZWDeleteReturnRoute = NULL;
    cbFuncZWSetLearnMode = NULL;
    cbFuncZWRequestNodeNodeNeighborUpdate = NULL;
    cbFuncZWSetSUCNodeID = NULL;
    cbFuncZWRequestNetworkUpdate = NULL;
    cbFuncZWSendSUCID = NULL;

    callbacks = (struct SerialAPI_Callbacks*) _callbacks;

#ifdef __ROUTER_VERSION__
    for(i=0; i < sizeof(timers) / sizeof(struct ZW_timer); i++) {
        ctimer_stop(&timers[i].timer);
        timers[i].f =0;
    }
#endif
    /*Get Capabilities of serial API*/
    byLen = 0;
    if(!SendFrameWithResponse(FUNC_ID_SERIAL_API_GET_CAPABILITIES, 0, 0 , buffer, &byLen )) {
      SER_PRINTF("SendFrameWithResponse(FUNC_ID_SERIAL_API_GET_CAPABILITIES,...) failed in SerialAPI_Init()\n");
      ASSERT(0);
      return FALSE;
    }
    memcpy(&capabilities, &(buffer[IDX_DATA]), byLen -IDX_DATA);
    //SER_PRINTF("Serial API version     : %d.%.2d\n",capabilities.appl_version,capabilities.appl_revision);

    byLen = 0;
    idx = 0;
    buffer[idx++] = SERIAL_API_SETUP_CMD_SUPPORTED;
    if(SupportsCommand(FUNC_ID_SERIALAPI_SETUP)) {
      if(!SendFrameWithResponse(FUNC_ID_SERIALAPI_SETUP, buffer, idx, buffer, &byLen )) {
        SER_PRINTF("SendFrameWithResponse(FUNC_ID_SERIALAPI_SETUP,...) failed in SerialAPI_Init()\n");
        ASSERT(0);
        return FALSE;
      }
      if (byLen > (IDX_DATA + 1)
          && buffer[IDX_DATA] == 1) {
        uint8_t current_index = IDX_DATA + 1;
        uint8_t len_to_copy = byLen - (current_index - 1);

        //Prevent array over run
        if (len_to_copy > sizeof(capabilities.supported_serialapi_bitmask)) {
          len_to_copy = sizeof(capabilities.supported_serialapi_bitmask);
        }

        memcpy(&capabilities.supported_serialapi_bitmask[0],
               &(buffer[current_index]),
               len_to_copy);
      }
    }

    type= ZW_Version(buf);
    if(type < sizeof(zw_lib_names) /sizeof(char*)) {
      SER_PRINTF("Z-Wave library version : %s\n",&buf[7]);
      SER_PRINTF("Z-Wave library type    : %s\n",zw_lib_names[type]);
    }

    
    // We dont really do anything with nodelist here so no need of calling 
    // SerialAPI_GetLRNodeList() for LR

    /*We call this to update our local cache of the chip type. */
    SerialAPI_GetInitData(&ver, &cap, &len, nodelist, &chip_type,
      &chip_version);

    if(callbacks->ApplicationInitHW) callbacks->ApplicationInitHW(0);
    if(callbacks->ApplicationInitSW) callbacks->ApplicationInitSW();

    if(callbacks->ApplicationNodeInformation) {
      callbacks->ApplicationNodeInformation(&listening,&nodeType,&nodeParm,&parmLength);
      SerialAPI_ApplicationNodeInformation(listening,nodeType,nodeParm,parmLength);
    }

    return TRUE;
}

void Get_SerialAPI_AppVersion(uint8_t *major, uint8_t *minor)
{
	*major = capabilities.appl_version;
	*minor = capabilities.appl_revision;

	return;
}

void SerialAPI_GetChipTypeAndVersion(uint8_t *type, uint8_t *version)
{
   *type = my_chip_data.chip_type;
   *version = my_chip_data.chip_version;
}

/**
 * DeInit the serialAPI and close com ports.
 */
void SerialAPI_Destroy() {
  my_chip_data.chip_type = 0;
  ConDestroy();
}

/*
 Give the number of outstanding packet in rxQueue
*/
int rxQueue_Len(void)
{
	 struct list *q=rxQueue;
	 int len = 0;

	 while(q)
	 {
		 q = q->next;
		 len++;
	 }
 	 return len;
}

/*
 Flush rxQueue
*/
void SerialFlushQueue(void) {
  struct list* l;
  while(rxQueue) {
    l = rxQueue;
    rxQueue = rxQueue->next;
#ifdef __ASIX_C51__
    zfree(l->data);
    zfree(l);
#else
    free(l->data);
    free(l);
#endif
  }
}

/**
 * TODO get rid of malloc
 */
static void QueueFrame() {
  struct list *l,*q;
  if(rxQueue_Len() >= MAX_RXQUEUE_LEN)
  {
	  //printf("rxQueue is Full !!!\r\n");
	  return;
  }
  #ifdef __ASIX_C51__
  l = (struct list*) zmalloc(sizeof(struct list));
  if(l == NULL)
  {
	printf("QueueFrame:malloc failed.Packet Loss !!!\r\n");
	return;
  }
  l->data = zmalloc(serBufLen);
  if(l->data == NULL)
  {
	printf("QueueFrame: malloc failed-2.Packet Loss !!!\r\n");
	return;
  }
  #else
  l = (struct list*) malloc(sizeof(struct list));
  l->data = malloc(sizeof(serBuf));
  #endif
  l->next = 0;
  l->len = serBufLen;
  memcpy(l->data,serBuf,serBufLen);

  /*Add frame to end of queue.*/
  if(rxQueue) {
    for(q=rxQueue; q->next; q = q->next);
    q->next = l;
  } else {
    rxQueue=l;
  }
#ifdef __ROUTER_VERSION__
  process_poll(&serial_api_process);
#endif
}


int WaitResponse() {
  int ret;
  PORT_TIMER t;
  PORT_TIMER_INIT(t,TIMEOUT_TIME);

  while( 1 ) {
    ret = ConUpdate(TRUE);
    if(ret != conIdle) {
      break;
    }

		if(PORT_TIMER_EXPIRED(t)) {
		  SER_PRINTF("WaitResponse Timeout\n");
		  ret=conRxTimeout;
		  break;
		}
	}
	return ret;
}
//#include"sys/clock.h"

//extern clock_time_t timeref;



static void DrainRX() {
  while( ConUpdate(TRUE) == conFrameReceived) {
    if(serBuf[1] == REQUEST) {
      QueueFrame();
    }
  }
}


/**
 * Send data frame to Z-Wave chip via serial API and wait for ACK.
 *
 * \param[in] cmd       Serial API command
 * \param[in] param_buf Byte array with serial API command parameters
 * \param[in] param_len Length in bytes of parameter array
 */
static int SendFrame(
  BYTE  cmd,
  BYTE *param_buf,
  BYTE  param_len)
{
	int i;
	enum T_CON_TYPE ret;
	PORT_TIMER t;
  if(!SupportsCommand(cmd)) {
    SER_PRINTF("Command: 0x%x is not supported by this SerialAPI\n",(unsigned)cmd);
	  ASSERT(0);
	  return conTxErr;
	}
  /*First check for incoming.*/
	DrainRX();

    ConTxFrame(cmd, REQUEST, param_buf, param_len);
	for(i=0; i < 20; i++) {
		ret=WaitResponse();
		switch(ret) {
		case conFrameSent:
		  return ret;
		case conFrameReceived:
          if (i) { 
            i--; // Do not count incoming frame as failed transmission attempt
          }
          if(serBuf[1] == REQUEST) {
            QueueFrame();
          }
          SER_PRINTF("Got RESPONSE frame while sending.... \n");
//      dump_serbuf();
      //Don't queue frame here because WaitResponse does it if its a request
      //QueueFrame();
		  /* If we received a frame here then we were both sending. The embedded target will have
		   * queued a CAN at this point, since we have been sending a frame to the uart buffer.
		   * before ACK'ing the received frame.
		   */
		  continue; /*Go back, there should be more*/
		case conTxWait:
          break;
		default:
          break;
		}
		//SER_PRINTF("Retransmission %i of %2x %s\n",i,cmd, ConTypeToStr(ret));

		SER_PRINTF("Retransmission %d of 0x%02x\n",i,cmd);

#ifdef __ASIX_C51__
		  //When SSL handshake is in progress, don't retry sending out.
		if((gIsSslHshakeInProgress == 1) && (i >= 3))
		{
		    SER_PRINTF("SSL in progress. No serial retries!!!!\r\n" );
		    break;
		}
#endif

		/* It seems that the serial port sometimes stalls on osx, this seems to help */
		if((i & 7) == 7) {
		  SER_PRINTF("Reopening serial port\n");
          SerialRestart();
		}
                /* This is a layer violation, since SerialFlush is not in conhandle */
		SerialFlush();
		/*TODO consider to use an exponential backoff, and do not backoff until our own framehandler is idle. Also
		 * the magnitude of the backoff seem very large... this is to be analyzed. */
        PORT_TIMER_INIT(t,10+i*100);
        while(! PORT_TIMER_EXPIRED(t)) {
          DrainRX();
        }
		ConTxFrame(cmd, REQUEST, param_buf, param_len);
	}
	SER_PRINTF("Unable to send frame!!!!!!\n");
	//ASSERT(0);
	return ret;
}

/**
 * Send data frame to Z-Wave chip via serial API and wait for ACK and RES
 *
 * \param[in]  cmd          Serial API command
 * \param[in]  param_buf    Byte array with serial API command parameters
 * \param[in]  param_len    Length in bytes of parameter array
 * \param[out] response_buf Buffer to hold response (buffer should be large enough to hold SERBUF_MAX bytes).
 * \param[out] response_len Length of response copied to response_buf.
 */
static int SendFrameWithResponse(
  BYTE  cmd,
  BYTE *param_buf,
  BYTE  param_len,
  BYTE *response_buf,
  BYTE *response_len )
{
	int ret;
	int i;

  ret = SendFrame(cmd, param_buf, param_len);
  if(ret == conFrameSent) {
    for(i=0; i < 3; i++) {
      ret = WaitResponse();
      if(ret==conFrameReceived) {
        if(serBuf[1] == REQUEST) {
          QueueFrame();
        } else {
          /* Did we get a response to the command we sent? */
          if( serBuf[ IDX_CMD ] == cmd ) {
            memcpy(response_buf, serBuf, serFrameLen);
            if (response_len) *response_len = serFrameLen;
            return ret;
          } else {
            /*This if for the case where we get a callback from another function instead of a response */
            //Dispatch(serBuf);
            SER_PRINTF("Got new frame 0x%x (not 0x%x) while sending %d\n", serBuf[IDX_CMD], cmd, i);
          }
        }
      } else {
        SER_PRINTF("Unexpected receive state! %s\n",ConTypeToStr(ret));
      }
    }
  }
  SER_PRINTF("SendFrameWithResponse() returning failure for cmd: 0x%2x\n", cmd);
  return 0;
}


extern BYTE transportServiceState;
extern BYTE bCompleteTimerHandle;
extern BYTE bRecoverTimer;

/**
 * \ingroup SerialAPI
 * Serial port polling loop.
 * This should be called continuously from a main loop. All callbacks are
 * called as a decendant of this loop.
 */
uint8_t SerialAPI_Poll(void)
{
  struct list* l;

  if(rxQueue) {
    l = rxQueue;
    rxQueue = rxQueue->next;
    serBufLen = l->len;
    memcpy(serBuf,l->data,l->len);
    Dispatch(l->data, l->len);
    free(l->data);
    free(l);
  }

  DrainRX();


  if(callbacks && callbacks->ApplicationPoll) callbacks->ApplicationPoll();

  return rxQueue>0;
}

/* Check if the received frame will overflow pCmd buffer */
BOOL                                      /* RET TRUE or FALSE */
DetectBufferOverflow(uint16_t len)        /* IN  length of frame */
{
    if (len > sizeof(pCmd))
    {
      SER_PRINTF("Buffer overflow, dropping SerialAPI frame\n");
      return TRUE;
    }
    return FALSE;
}

/**
 * Return the minimum of two unsigned numbers.
 * Preferring function to macro to avoid double evaluation. No need to performance optimize
 * to preprocessor macro.
 *
 * Cannot find this in standard library,
 */
static unsigned int min(unsigned int x, unsigned int y)
{
  if (y < x)
  {
    return y;
  }
  return x;
}

/*************************************************************************************************/



/**
  * \ingroup SerialAPI
  * Execute a callback based on the received frame.
  * NOTE: Must only be called locally in the Serial API.
  * \param[in] pData    Pointer to data frame (without SOF)
  * \param[in] byLen    Length of data frame
  */
static void Dispatch( BYTE *pData , uint16_t len)
{
  int i;
  void ( *funcLearnInfo )( LEARN_INFO* )=0;
  VOID_CALLBACKFUNC(f)(BYTE, TX_STATUS_TYPE*);
  VOID_CALLBACKFUNC(f2)(BYTE);


  i=0;
  // Detect runt packets and drop them.
  if (len <= IDX_DATA) {
    ERR_PRINTF("Dropping invalid runt packet\n");
    return;
  }
  if (pData[1]==1) return;

  //printf("xxxxxxx %d      xxxxxxxxxxxxxxx\n",pData[ IDX_CMD ]);
  switch ( pData[ IDX_CMD ] )
  {

    case FUNC_ID_ZW_APPLICATION_CONTROLLER_UPDATE:
      {
          uint8_t bStatus;
          // Move DetectBufferOverflow to where lengths are determined below
          if ( callbacks->ApplicationControllerUpdate == NULL)
          {
            break;
          }


          bStatus = pData[ IDX_DATA ];
          /* */
          if ((bStatus == UPDATE_STATE_NODE_INFO_FOREIGN_HOMEID_RECEIVED) ||
              (bStatus == UPDATE_STATE_NODE_INFO_SMARTSTART_HOMEID_RECEIVED_LR))
          {
            /* ZW->HOST: 0x49 | 0x85 | prospectID (NB: 16 bit in LR mode, 8 bit otherwise) | bLen
                           | prospectHomeID[0] | prospectHomeID[1] | prospectHomeID[2] | prospectHomeID[3]
                           | nodeInfoLen | BasicDeviceType | GenericDeviceType | SpecificDeviceType
                           | nodeInfo[nodeInfoLen]
             */
            uint8_t prospectHomeID[4], nodeInfoLen, length, *pAppNodeInfo;
            uint16_t prospectID = 0;
            uint8_t idx = IDX_DATA + 1;
            // This checks that everything upto the NIF is there. 
            if (len < (IDX_CMD + 8)) {
              ERR_PRINTF("INIF length < 10\n");
              break;
            }
            int j = 0;
            if (lr_enabled) {
              prospectID = pData[idx + (j++)] << 8;  //j is 0 (1 in LR)
            }
            prospectID |= pData[idx + (j++)];  //j is 1 (2 in LR)
            
            // TODO: Check packet parsing with regards to specifications. RX status at bit 7 may be incorrectly blocking INIF receive.
            uint8_t rx_status = pData[idx + (j++)];  //j is 2 (3 in LR)

            memcpy(prospectHomeID, &pData[idx + j ], sizeof prospectHomeID);  //j is 3 (4 in LR)
            idx += j + sizeof prospectHomeID;
            length = 3 + pData[idx++];
            
            if (idx + length > len) {
              ERR_PRINTF("NIF length=%d > number of received bytes=%d.\n", idx + length, len);
              break;
            }
            memcpy(pCmd, &pData[idx], length);

            callbacks->ApplicationControllerUpdate(bStatus,
                prospectID,
                pCmd,
                length,
                prospectHomeID);
          }
          else if (bStatus == UPDATE_STATE_INCLUDED_NODE_INFO_RECEIVED)
          {
            /* ZW-HOST: FUNC_ID_ZW_APPLICATION_UPDATE | UPDATE_STATE_INCLUDED_NODE_INFO_RECEIVED | bSource |
                  bINIFrxStatus | abINIFsmartStartNWIHomeID[4]

               or in LR mode:
               ZW-HOST: FUNC_ID_ZW_APPLICATION_UPDATE | UPDATE_STATE_INCLUDED_NODE_INFO_RECEIVED | bSource MSB | bSource LSB |
                  bINIFrxStatus | abINIFsmartStartNWIHomeID[4]
             */
            unsigned j = IDX_DATA + 2;
            uint16_t srcId = 0;
            if (lr_enabled) {
                srcId = pData[j++] << 8;
            }
            srcId |= pData[j++];

            if((j+5) > len) // Stop processing if the length is larger than the buffer.
              break;

            memcpy(pCmd, &pData[j], 5);
            callbacks->ApplicationControllerUpdate(bStatus,
                srcId,
                pCmd,
                5, /* no variable length here */
                NULL); /* IN this case, homeid is embedded in pCmd*/
          }
          else
          {
            /* ZW->HOST: 0x49 | bStatus | srcID | bLen
                          | BasicDeviceType | GenericDeviceType | SpecificDeviceType
                          | nodeInfo
                         */
            int j = 1;
            if (len < (IDX_CMD + (lr_enabled ? 3 : 2))) {
              ERR_PRINTF("Application Update Command does not have sufficient data.\n");
              break;
            }
            memset(&learnNodeInfo, 0, sizeof(learnNodeInfo));
            learnNodeInfo.bStatus = pData[ IDX_DATA ];

            if (lr_enabled) {
              learnNodeInfo.bSource = pData[IDX_DATA + (j++)] << 8;  //j is (1 in LR)
            }
            learnNodeInfo.bSource |= pData[IDX_DATA + (j++)];  //j is 1(2 in LR)

            learnNodeInfo.bLen = pData[ IDX_DATA + (j++) ]; //j is 2 (3 in LR)
            // Stop processing if the length is larger than the buffer.
            if(DetectBufferOverflow(learnNodeInfo.bLen) || (learnNodeInfo.bLen + IDX_DATA + j) > len)
              break;

            for ( i = 0; i < learnNodeInfo.bLen; i++ )
              pCmd[i] = pData[IDX_DATA + j + i]; //j is 3 (4 in LR)

            learnNodeInfo.pCmd = pCmd;
            callbacks->ApplicationControllerUpdate(learnNodeInfo.bStatus,
                                            learnNodeInfo.bSource,
                                            learnNodeInfo.pCmd,
                                            learnNodeInfo.bLen,
                                            NULL);
          }
      }
      break;

    case FUNC_ID_APPLICATION_COMMAND_HANDLER:
      {
        if(callbacks->ApplicationCommandHandler) {
          // Move DetectBufferOverflow to length calculation below.

          /* ZW->HOST: REQ | 0x04 | rxStatus | sourceNode | cmdLength | pCmd[]
           *        | rxRSSIVal | securityKey
           */
          uint16_t source_node = 0;
          int j = 1;
          if ( len <= (IDX_DATA + (lr_enabled ? 3 : 2))) {
            ERR_PRINTF("FUNC_ID_APPLICATION_COMMAND_HANDLER insufficient data.\n");
            break;
          }
          if (lr_enabled) {
            source_node = (pData[IDX_DATA + (j++)]) << 8; //j is (1 in LR)
          }
          source_node |= pData[IDX_DATA + (j++)]; //j is 1 (2 in LR)
          if(nodemask_nodeid_is_invalid(source_node)) {
            if(is_nodeid_basetype_8() && lr_enabled) {
              SER_PRINTF("Invalid node id: %d received because Long Range is enabled and Nodeid base type is 8. Setting it to 16", source_node);
              SerialAPI_EnableLR();
            }
          }
          // Rename variable 'len' that hides/shadows function parameter
          uint8_t length = pData[ IDX_DATA + (j++) ]; //j is 2 (3 in LR)
          if (DetectBufferOverflow(length) || (IDX_DATA + j + length) > len) // Stop processing if the length is larger than the buffer.
            break;

          for ( i = 0;i < length;i++ )
              pCmd[ i ] = pData[ IDX_DATA + (j) + i ];//j is 3 (4 in LR)

          callbacks->ApplicationCommandHandler( pData[ IDX_DATA ], 0, source_node, (ZW_APPLICATION_TX_BUFFER*)pCmd, length);
        }
      }
      break;

 
    case FUNC_ID_PROMISCUOUS_APPLICATION_COMMAND_HANDLER:
      {
        //FIXME: FuncID Not changed in LR?
        if(lr_enabled) {
           assert(0);
        }
        if(callbacks->ApplicationCommandHandler) {
          if (DetectBufferOverflow(pData[IDX_DATA + 2]) || (IDX_DATA + 3 + pData[IDX_DATA+2]) > len) // Stop processing if the length is larger than the buffer.
            break;

          for ( i = 0;i < pData[ IDX_DATA + 2 ];i++ )
              pCmd[ i ] = pData[ IDX_DATA + 3 + i ];
          callbacks->ApplicationCommandHandler( pData[ IDX_DATA ],pData[ byLen-1 ] , pData[ IDX_DATA + 1 ], (ZW_APPLICATION_TX_BUFFER*)pCmd, pData[ IDX_DATA + 2 ] );
        }
      }
      break;

    case FUNC_ID_APPLICATION_COMMAND_HANDLER_BRIDGE:
      {
        if(callbacks->ApplicationCommandHandler_Bridge) {
          // DetectBufferOverflow moved below.

          uint16_t source_node = 0, dest_node = 0;
          // Rename variable 'len' that hides/shadows function parameter
          uint8_t length = 0;
          /* ZW->HOST: 
           * REQ | 0xA8 | rxStatus | destNodeID | srcNodeID | cmdLength |
           *            pCmd[ ] | multiDestsOffset_NodeMaskLen | 
           *            multiDestsNodeMask | rxRSSIVal */
          int j = 1;
          // Verify data size.
          if ( len <= (IDX_DATA + (lr_enabled ? 5 : 3))) {
              ERR_PRINTF("FUNC_ID_APPLICATION_COMMAND_HANDLER_BRIDGE insufficient data.\n");
              break;
          }
          if(lr_enabled) {
              dest_node = (pData[IDX_DATA + (j++)]) << 8; //j is (1 in LR)
          }
          dest_node |= pData[IDX_DATA + (j++)]; //j is 1 ( 2 in LR)
          if (lr_enabled) {
              source_node = (pData[IDX_DATA + (j++)]) << 8; //j is( 3 in LR)
          }
          source_node |= pData[IDX_DATA + (j++)]; //j is 2 ( 4 in LR)
          length =  pData[IDX_DATA + (j++)]; //j is 3 ( 5 in LR)
          if (DetectBufferOverflow(length) || (IDX_DATA+j+length) > len) // Stop processing if the length is larger than the buffer.
            break;
          for (i = 0; i < length; i++)
              pCmd[ i ] = pData[ IDX_DATA + j + i ]; //j is 4 ( 6 in LR)

          callbacks->ApplicationCommandHandler_Bridge(
            pData[IDX_DATA],
            dest_node,
            source_node,
            (ZW_APPLICATION_TX_BUFFER*)pCmd, length);
        }
      }
      break;

#if 0  /* ZIP Router bridge does not support 300 series */
    case FUNC_ID_APPLICATION_SLAVE_COMMAND_HANDLER:   /* pre 6.0x only */
      {
        if(callbacks->ApplicationSlaveCommandHandler)
        {
          for ( i = 0;i < pData[ IDX_DATA + 3 ];i++ )
          pCmd[ i ] = pData[ IDX_DATA + 4 + i ];
          callbacks->ApplicationSlaveCommandHandler( pData[ IDX_DATA ],pData[IDX_DATA + 1],
              pData[IDX_DATA + 2], (ZW_APPLICATION_TX_BUFFER*)pCmd, pData[IDX_DATA + 3] );
        }
      }
      break;
#endif

          // The rest are callback functions

    case FUNC_ID_ZW_SEND_NODE_INFORMATION:
      if ( cbFuncZWSendNodeInformation != NULL )
      {
          cbFuncZWSendNodeInformation( pData[ IDX_DATA + 1 ] );
      }
      break;

    case FUNC_ID_ZW_SEND_DATA_BRIDGE:
    case FUNC_ID_ZW_SEND_DATA:
      {
        TX_STATUS_TYPE txStatusReport;
        uint8_t* p = &pData[ IDX_DATA + 1 ];
        uint8_t txStatus = *p++;

        if(pData[ IDX_CMD ] == FUNC_ID_ZW_SEND_DATA) {
          f = cbFuncZWSendData;
          cbFuncZWSendData = 0;
        } else {
          f = cbFuncZWSendDataBridge;
          cbFuncZWSendDataBridge = 0;
        }

        if(len>=24) {
          txStatusReport.wTransmitTicks = (p[0] <<8) | (p[1] <<0);
          p+=2;
          txStatusReport.bRepeaters = *p++ ;
          txStatusReport.rssi_values.incoming[0] = *p++;
          txStatusReport.rssi_values.incoming[1] = *p++;
          txStatusReport.rssi_values.incoming[2] = *p++;
          txStatusReport.rssi_values.incoming[3] = *p++;
          txStatusReport.rssi_values.incoming[4] = *p++;
          txStatusReport.bACKChannelNo = *p++;
          txStatusReport.bLastTxChannelNo = *p++;
          txStatusReport.bRouteSchemeState = *p++;
          txStatusReport.pLastUsedRoute[0] = *p++;
          txStatusReport.pLastUsedRoute[1] = *p++;
          txStatusReport.pLastUsedRoute[2] = *p++;
          txStatusReport.pLastUsedRoute[3] = *p++;
          txStatusReport.pLastUsedRoute[4] = *p++;
          txStatusReport.bRouteTries = *p++;
          txStatusReport.bLastFailedLink.from=*p++;
          txStatusReport.bLastFailedLink.to=*p++;
          if (len >= 29) {
            txStatusReport.bUsedTxpower = *p++;
            txStatusReport.bMeasuredNoiseFloor = *p++;
            txStatusReport.bAckDestinationUsedTxPower = *p++;
            txStatusReport.bDestinationAckMeasuredRSSI = *p++;
            txStatusReport.bDestinationckMeasuredNoiseFloor = *p++;
          }

          if(f) {
            f( txStatus ,&txStatusReport );
          }
        } else {
          if(f) {
            f( txStatus,0 );
          }
        }
      }
      break;

    case FUNC_ID_ZW_SEND_TEST_FRAME:
      {
          if ( cbFuncZWSendTestFrame != NULL ) {
            f2 = cbFuncZWSendTestFrame;
            cbFuncZWSendTestFrame = 0;
            f2( pData[ IDX_DATA + 1 ] );
          }
      }
      break;
    case FUNC_ID_ZW_SEND_DATA_MULTI_BRIDGE:
      if ( cbFuncZWSendDataMultiBridge != NULL )
      {
          /* (pData + IDX_DATA)[] contains { funcId, txStatus }. Only txStatus is used */
          cbFuncZWSendDataMultiBridge(pData[IDX_DATA + 1]);
      }
      break;

    case FUNC_ID_MEMORY_PUT_BUFFER:
      if ( cbFuncMemoryPutBuffer != NULL )
      {
          cbFuncMemoryPutBuffer();
      }
      break;

    case FUNC_ID_ZW_SET_DEFAULT:
      if ( cbFuncZWSetDefault != NULL )
      {
          cbFuncZWSetDefault();
      }
      break;

    case FUNC_ID_ZW_CONTROLLER_CHANGE:
    case FUNC_ID_ZW_CREATE_NEW_PRIMARY:
    case FUNC_ID_ZW_REMOVE_NODE_FROM_NETWORK:
    case FUNC_ID_ZW_ADD_NODE_TO_NETWORK:
      switch(pData[ IDX_CMD ]) {
      case FUNC_ID_ZW_CONTROLLER_CHANGE:
        funcLearnInfo = cbFuncZWControllerChange;
        break;
      case FUNC_ID_ZW_CREATE_NEW_PRIMARY:
        funcLearnInfo = cbFuncZWNewController;
        break;
      case FUNC_ID_ZW_REMOVE_NODE_FROM_NETWORK:
        funcLearnInfo = cbFuncRemoveNodeFromNetwork;
        break;
      case FUNC_ID_ZW_ADD_NODE_TO_NETWORK:
        funcLearnInfo = cbFuncAddNodeToNetwork;
        break;
      }

      if ( funcLearnInfo != NULL )
      {

         /* ZW->HOST: REQ | 0x4A | funcID | bStatus | bSource | bLen | basic |
          *             generic | specific | cmdclasses[ ]
          */
        int i = 1;
        memset(&learnNodeInfo,0,sizeof(learnNodeInfo));
        learnNodeInfo.bStatus = pData[ IDX_DATA + (i++) ];

        if ((learnNodeInfo.bStatus==ADD_NODE_STATUS_ADDING_END_NODE) ||
            (learnNodeInfo.bStatus==ADD_NODE_STATUS_ADDING_CONTROLLER))
        {
          if ( len <= (IDX_DATA + (lr_enabled ? 5 : 3))) {
              ERR_PRINTF("FUNC_ID_ZW_ADD_NODE_TO_NETWORK does not have sufficient data.\n");
              break;
          }
          if (lr_enabled) {
            learnNodeInfo.bSource = pData[ IDX_DATA + (i++) ] << 8; 
          }
          learnNodeInfo.bSource |= pData[ IDX_DATA + (i++) ]; 
          learnNodeInfo.bLen = pData[ IDX_DATA + (i++) ];
          learnNodeInfo.pCmd = pCmd;
          if (DetectBufferOverflow(learnNodeInfo.bLen) || (IDX_DATA+i+learnNodeInfo.bLen) > len)
            break;
          memcpy(pCmd,&pData[ IDX_DATA +(i++)] ,learnNodeInfo.bLen);
        }
   /*
          if (learnNodeInfo.bStatus==0x6)
            cbFuncZWNewController( &learnNodeInfo );
     */

          funcLearnInfo( &learnNodeInfo );
      }

      break;

    case FUNC_ID_ZW_REPLICATION_SEND_DATA:
      if ( cbFuncZWReplicationSendData != NULL )
      {
          cbFuncZWReplicationSendData( pData[ IDX_DATA + 1 ] );
      }
      break;

    case FUNC_ID_ZW_ASSIGN_RETURN_ROUTE:
      if ( cbFuncZWAssignReturnRoute != NULL )
      {
          cbFuncZWAssignReturnRoute( pData[ IDX_DATA + 1 ] );
      }
      break;

    case FUNC_ID_ZW_DELETE_RETURN_ROUTE:
      if ( cbFuncZWDeleteReturnRoute != NULL )
      {
          cbFuncZWDeleteReturnRoute( pData[ IDX_DATA + 1 ] );
      }
      break;

    case FUNC_ID_ZW_ASSIGN_SUC_RETURN_ROUTE:
      if ( cbFuncZWAssignSUCReturnRoute != NULL )
      {
          cbFuncZWAssignSUCReturnRoute( pData[ IDX_DATA + 1 ] );
      }
      break;

    case FUNC_ID_ZW_SEND_SUC_ID:
     if ( cbFuncZWSendSUCID != NULL )
     {
         cbFuncZWSendSUCID( pData[ IDX_DATA + 1 ], 0 );
     }
    break;

    case FUNC_ID_ZW_DELETE_SUC_RETURN_ROUTE:
      if ( cbFuncZWDeleteSUCReturnRoute != NULL )
      {
          cbFuncZWDeleteSUCReturnRoute( pData[ IDX_DATA + 1 ] );
      }
      break;

    case FUNC_ID_ZW_SET_LEARN_MODE:
      if ( cbFuncZWSetLearnMode != NULL )
      {
          /* ZW->HOST: REQ | 0x50 | funcID | bStatus | bSource | bLen | pCmd[ ]
           */
          int j = 1;
          if ( len <= (IDX_DATA + (lr_enabled ? 4 : 3))) {
            ERR_PRINTF("FUNC_ID_ZW_SET_LEARN_MODE does not have sufficient data.\n");
            break;
          }
          memset(&learnNodeInfo, 0, sizeof(learnNodeInfo));
          learnNodeInfo.bStatus = pData[ IDX_DATA + (j++) ]; //j is 1
          if (lr_enabled) {
            learnNodeInfo.bSource = pData[ IDX_DATA + (j++) ] << 8; // j is  (2 in LR) 
          }
          learnNodeInfo.bSource |= pData[ IDX_DATA + (j++) ]; // j is 2 (3 in LR) 
          learnNodeInfo.bLen = pData[ IDX_DATA + (j++) ]; // j is 3 (4 in LR)
          // Check the proper length byte for overflows
          if (DetectBufferOverflow(learnNodeInfo.bLen) || (IDX_DATA+j+learnNodeInfo.bLen)>len)
            break;

          for ( i = 0;i < learnNodeInfo.bLen; i++ )
          {
              pCmd[ i ] = pData[ IDX_DATA + j + i ]; // j is 4 ( 5 in LR)
          }
          learnNodeInfo.pCmd = pCmd;

          cbFuncZWSetLearnMode( &learnNodeInfo /*learnNodeInfo.bStatus, learnNodeInfo.bSource */);
      }
      break;

    case FUNC_ID_ZW_SET_SLAVE_LEARN_MODE:
      if ( cbFuncZWSetSlaveLearnMode != NULL )
      {
        int j = 2;
        uint16_t onode = 0, nnode = 0;
        onode = pData[ IDX_DATA + (j++) ];
        nnode |= pData[ IDX_DATA + (j++) ];

        cbFuncZWSetSlaveLearnMode(pData[ IDX_DATA + 1 ], onode, nnode);
      }
      break;

    case FUNC_ID_ZW_SET_SUC_NODE_ID:
      if ( cbFuncZWSetSUCNodeID != NULL )
      {
          cbFuncZWSetSUCNodeID( pData[ IDX_DATA + 1 ] );
      }
      break;

    case FUNC_ID_ZW_REQUEST_NODE_NEIGHBOR_UPDATE:
      if ( cbFuncZWRequestNodeNodeNeighborUpdate != NULL )
      {
          cbFuncZWRequestNodeNodeNeighborUpdate( pData[ IDX_DATA + 1 ] );
      }
      break;

    case FUNC_ID_ZW_REQUEST_NETWORK_UPDATE:
      if ( cbFuncZWRequestNetworkUpdate != NULL )
      {
          cbFuncZWRequestNetworkUpdate( pData[ IDX_DATA + 1 ] );
      }
      break;
    case FUNC_ID_ZW_REMOVE_FAILED_NODE_ID:
      if( cbFuncZWRemoveFailedNode != NULL )
      {
        cbFuncZWRemoveFailedNode( pData[ IDX_DATA + 1 ]);
      }
      break;
    case FUNC_ID_ZW_REPLACE_FAILED_NODE:
      if( cbFuncZWReplaceFailedNode != NULL )
      {
        cbFuncZWReplaceFailedNode( pData[ IDX_DATA + 1 ]);
      }
      break;

    case FUNC_ID_SERIALAPI_STARTED:

      lr_enabled = 0;
      /* ZW->HOST: bWakeupReason | bWatchdogStarted | deviceOptionMask | */
      /*           nodeType_generic | nodeType_specific | cmdClassLength | cmdClass[] */
      // Do not issue the callback if the packet size is larger than our buffer.
      if ( callbacks->SerialAPIStarted != NULL &&  !(len <= (IDX_DATA+2) || DetectBufferOverflow(pData[ IDX_DATA + 2 ]) || (IDX_DATA+3+pData[IDX_DATA+2])>len ))
      {
        for ( i = 0;i < pData[ IDX_DATA + 2 ];i++ )
                      pCmd[ i ] = pData[ IDX_DATA + 3 + i ];
        callbacks->SerialAPIStarted(pCmd, pData [IDX_DATA + 2]);
      }

      if (ZW_GECKO_CHIP_TYPE(my_chip_data.chip_type)) {
        SerialAPI_WatchdogStart();
      }

      break;

    default:
      SER_PRINTF("Unknown SerialAPI FUNC_ID: 0x%02x\n",pData[ IDX_CMD ]);
    }

}

/**
  * \ingroup SerialAPI
  * \defgroup ZWCMD Z-Wave Commands
  */

/**
  * \ingroup ZWCMD
  * Initialize the Z-Wave RF chip.
  * \param[in] mode
  * = TRUE : Set the RF chip in receive mode and starts the data sampling. \n
  * = FALSE : Set the RF chip in power down mode. \n
  */
BYTE ZW_SetRFReceiveMode( BYTE mode )
{
    byLen = 0;
    idx=0;
    buffer[ idx++ ] = mode;
    SendFrameWithResponse(FUNC_ID_ZW_SET_RF_RECEIVE_MODE, buffer, idx, buffer, &byLen );

    return buffer[ IDX_DATA ];
}

/*==========================   ZW_RFPowerLevelSet  ==========================
**    Set the powerlevel used in RF transmitting.
**    Valid powerlevel values are :
**
**       normalPower : Max power possible
**       minus2dBm    - normalPower - 2dBm
**       minus4dBm    - normalPower - 4dBm
**       minus6dBm    - normalPower - 6dBm
**       minus8dBm    - normalPower - 8dBm
**       minus10dBm   - normalPower - 10dBm
**       minus12dBm   - normalPower - 12dBm
**       minus14dBm   - normalPower - 14dBm
**       minus16dBm   - normalPower - 16dBm
**       minus18dBm   - normalPower - 18dBm
**
**--------------------------------------------------------------------------*/
BYTE                    /*RET The powerlevel set */
ZW_RFPowerLevelSet(
  BYTE powerLevel)      /* IN Powerlevel to set */
{
  idx = 0;
  byLen = 0;
  buffer[idx++] = powerLevel;
  SendFrameWithResponse(FUNC_ID_ZW_RF_POWER_LEVEL_SET,buffer, idx, buffer, &byLen);
  return buffer[ IDX_DATA ];
}
/*==========================   ZW_TXPowerLevelSet  ==========================
**    Set the current TX powerlevel, namely NormalTxPower and Measured0dBmPower
**
**    NormalTxPower:
**      The power level used when transmitting frames at normal power.
**      The power level is in deci dBm. E.g. 1dBm output power will be 10
**      in NormalTxPower and -2dBm will be -20 in NormalTxPower
**    Measured0dBmPower:
**      The output power measured from the antenna when NormalTxPower is
**      set to 0dBm. The power level is in deci dBm. E.g. 1dBm output power
**      will be 10 in Measured0dBmPower and -2dBm will be -20 in
**      Measured0dBmPower.
**
**--------------------------------------------------------------------------*/
/**
 * Set the current TX powerlevel
 * \return
 * 0 - failed, 1 - success
 * \sa ZW_TXPowerLevelSet
 *
 * \serialapi{
 * HOST->ZW: REQ | 0x0B | 0x04 | NormalTxPower | Measured0dBmPower
 * ZW->HOST: RES | 0x0B | 0x04 | CmdRes
 * }
 */
BYTE
ZW_TXPowerLevelSet(
  TX_POWER_LEVEL txpowerlevel)
{
  idx = 0;
  byLen = 0;
  buffer[idx++] = SERIAL_API_SETUP_CMD_TX_POWERLEVEL_SET;
  buffer[idx++] = txpowerlevel.normal;
  buffer[idx++] = txpowerlevel.measured0dBm;
  SendFrameWithResponse(FUNC_ID_SERIALAPI_SETUP, buffer, idx, buffer, &byLen);
  return buffer[IDX_DATA + 1];
}
/*==========================   ZW_MAXLRTXPowerLevelSet  ==========================
**    Set the current Max LR TX powerlevel,
**
**--------------------------------------------------------------------------*/
/**
 * Set the current MAX LR TX powerlevel
 * \return
 * 0 - failed, 1 - success
 * \sa ZW_TXPowerLevelSet
 *
 * \serialapi{
 * HOST->ZW: REQ | 0x0B | 0x03 | MAX LR TxPower MSB | MAX LR TxPower LSB
 * ZW->HOST: RES | 0x0B | 0x03 | CmdRes
 * }
 */
BYTE
ZW_MAXLRTXPowerLevelSet(
  int16_t max_lr_txpowerlevel)
{
  idx = 0;
  byLen = 0;
  buffer[idx++] = SERIAL_API_SETUP_CMD_MAX_LR_TX_PWR_SET;
  buffer[idx++] = max_lr_txpowerlevel >> 8;
  buffer[idx++] = max_lr_txpowerlevel & 0xff;
  SendFrameWithResponse(FUNC_ID_SERIALAPI_SETUP, buffer, idx, buffer, &byLen);
  return buffer[IDX_DATA + 1];
}


/*==========================   ZW_RFRegionSet  ==========================
**    Set the current RF region
**    Valid RF region values are :
**
**       EU          -    0x00
**       US          -    0x01
**       ANZ         -    0x02
**       HK          -    0x03
**       Malaysia    -    0x04
**       India       -    0x05
**       Israel      -    0x06
**       Russia      -    0x07
**       China       -    0x08
**       Japan       -    0x20
**       Korea       -    0x21
**
**--------------------------------------------------------------------------*/
/**
 * Set the current RF region setting to RFRegion
 * \return
 * 0 - failed, 1 - success
 * \sa ZW_RFRegionSet
 *
 * \serialapi{
 * HOST->ZW: REQ | 0x0B | 0x40 | RFRegion
 * ZW->HOST: RES | 0x0B | 0x40 | CmdRes
 * }
 */
BYTE
ZW_RFRegionSet(
  BYTE rfregion)
{
  /* Block changing RF Region to LR if the module does not support */
  if ((rfregion == RF_US_LR) &&
      !SerialAPI_SupportsLR()) {
    SER_PRINTF("Serial API: Cannot set Long Range RF Region 0x%02X setting without firmware support\n", rfregion);
    return 0;
  }
  idx = 0;
  byLen = 0;
  buffer[idx++] = SERIAL_API_SETUP_CMD_RF_REGION_SET;
  buffer[idx++] = rfregion;
  SendFrameWithResponse(FUNC_ID_SERIALAPI_SETUP, buffer, idx, buffer, &byLen);
  return buffer[IDX_DATA + 1];
}

/**
*    Create and transmit a node information broadcast frame
*    \retval FALSE if transmitter queue overflow
*    \retval FALSE if transmitter queue overflow
*/
BYTE                            /*RET */
ZW_SendNodeInformation(
  uint16_t destNode,                /*IN  Destination Node ID  */
  BYTE txOptions,               /*IN  Transmit option flags         */
  VOID_CALLBACKFUNC(completedFunc)(/*uto*/ BYTE))  /*IN  Transmit completed call back function  */
{
  idx = 0;
  byCompletedFunc = (completedFunc == NULL ? 0 : 0x03);
  set_node_id_in_buffer(destNode);
  buffer[idx++] = txOptions;
  buffer[idx++] = byCompletedFunc;      // Func id for CompletedFunc
  cbFuncZWSendNodeInformation = completedFunc;
  SendFrame(FUNC_ID_ZW_SEND_NODE_INFORMATION,buffer, idx);
  return  0;
}


/**===============================   ZW_SendData   ===========================
**    Transmit data buffer to a single ZW-node or all ZW-nodes (broadcast).
**

** @param[in] nodeID        Destination node ID (0xFF == broadcast)
** @param[in] pData         Data buffer pointer
** @param[in] dataLength    Data buffer length
** @param[in] txOptions     Transmit option flags
**          TRANSMIT_OPTION_LOW_POWER     transmit at low output power level
**                                        (1/3 of normal RF range).
**          TRANSMIT_OPTION_ACK           the multicast frame will be followed
**                                        by a  singlecast frame to each of
**                                        the destination nodes
**                                        and request acknowledge from each
**                                        destination node.
**          TRANSMIT_OPTION_AUTO_ROUTE    request retransmission via repeater
**                                        nodes at normal output power level).
** @param[in] completedFunc Transmit completed call back function
** @param[in] txStatus      IN Transmit status
**
**    @param txOptions:
**
** @retval FALSE if transmitter queue overflow
**--------------------------------------------------------------------------*/
BYTE                          /*RET  FALSE if transmitter busy      */
ZW_SendData(
  uint16_t nodeID,               /*IN  Destination node ID (0xFF == broadcast) */
  BYTE *pData,                /*IN  Data buffer pointer           */
  BYTE  dataLength,           /*IN  Data buffer length            */
  BYTE  txOptions,            /*IN  Transmit option flags         */
  VOID_CALLBACKFUNC(completedFunc)(BYTE, TX_STATUS_TYPE*)
  ) /*IN  Transmit completed call back function  */
{
  static int txnr =0;
  uint8_t i;

  if ((dataLength + 2) > sizeof(buffer)) {
    SER_PRINTF("ZW_SendData: Frame is too long\n");
    ASSERT(0);
    return FALSE;
  }

  //ASSERT(cbFuncZWSendData==0);
  idx = 0;
  byLen = 0;
  byCompletedFunc = (completedFunc == NULL ? 0 : (1 +(txnr & 0xf7)));
  set_node_id_in_buffer(nodeID);
  buffer[idx++] = dataLength;
  for (i = 0; i < dataLength; i++)
  {
    buffer[idx++] = pData[i];
  }
  buffer[idx++] = txOptions;
  buffer[idx++] = byCompletedFunc;      // Func id for CompletedFunc
  cbFuncZWSendData = completedFunc;
  if(SendFrameWithResponse(FUNC_ID_ZW_SEND_DATA,buffer, idx , buffer, &byLen) != conFrameReceived) {
    buffer[IDX_DATA] = FALSE;
    SER_PRINTF("Fail\n");
  } else {
    txnr++;
  }

  if(buffer[IDX_DATA] != TRUE) {
    SER_PRINTF("SendData fail\n");
    cbFuncZWSendData = 0;
  }

  return buffer[IDX_DATA];
}



BYTE               /*RET FALSE if transmitter busy else TRUE */
ZW_SendTestFrame(
  uint16_t nodeID,     /* IN nodeID to transmit to */
  BYTE powerLevel, /* IN powerlevel index */
  VOID_CALLBACKFUNC(func)(BYTE txStatus)) /* Call back function called when done */
{
  byCompletedFunc = (func == NULL ? 0 : 4);
  cbFuncZWSendTestFrame = func;
  idx = 0;
  byLen = 0;
  set_node_id_in_buffer(nodeID);
  buffer[idx++] = powerLevel;
  buffer[idx++] = byCompletedFunc;
  SendFrameWithResponse(FUNC_ID_ZW_SEND_TEST_FRAME,buffer, idx, buffer, &byLen);
  return buffer[ IDX_DATA ];
}

/**===============================   ZW_SendData_Bridge   ========================
**    Transmit data buffer to a single ZW-node or all ZW-nodes (broadcast)
**    from a virtual node.
**

** @param[in] srcNodeID        Destination node ID (0xFF == broadcast)
** @param[in] dstNodeID        Vurtual source node ID
** @param[in] pData         Data buffer pointer
** @param[in] dataLength    Data buffer length
** @param[in] txOptions     Transmit option flags
**          TRANSMIT_OPTION_LOW_POWER     transmit at low output power level
**                                        (1/3 of normal RF range).
**          TRANSMIT_OPTION_ACK           the multicast frame will be followed
**                                        by a  singlecast frame to each of
**                                        the destination nodes
**                                        and request acknowledge from each
**                                        destination node.
**          TRANSMIT_OPTION_AUTO_ROUTE    request retransmission via repeater
**                                        nodes at normal output power level).
** @param[in] completedFunc Transmit completed call back function
** @param[in] txStatus      IN Transmit status
**
**    @param txOptions:
**
** @retval FALSE if transmitter queue overflow
**--------------------------------------------------------------------------*/
BYTE                          /*RET  FALSE if transmitter busy      */
ZW_SendData_Bridge(
  uint16_t srcNodeID,            /*IN  Virtual source node ID*/
  uint16_t destNodeID,           /*IN  Destination node ID (0xFF == broadcast) */
  BYTE *pData,                /*IN  Data buffer pointer           */
  BYTE  dataLength,           /*IN  Data buffer length            */
  BYTE  txOptions,            /*IN  Transmit option flags         */
  VOID_CALLBACKFUNC(completedFunc)(BYTE, TX_STATUS_TYPE*)) /*IN  Transmit completed call back function  */
{
  static int txnr =0;
  int i;

  if ((dataLength + 2) > sizeof(buffer)) {
    SER_PRINTF("ZW_SendData: Frame is too long\n");
    ASSERT(0);
    return FALSE;
  }
//  assert(srcNodeID!=0xFF);
  idx = 0;
  byLen = 0;
  byCompletedFunc = (completedFunc == NULL ? 0 : (1 +(txnr & 0xf7)));

  set_node_id_in_buffer(srcNodeID);
  set_node_id_in_buffer(destNodeID);
  buffer[idx++] = dataLength;
  for (i = 0; i < dataLength; i++)
  {
    buffer[idx++] = pData[i];
  }
  buffer[idx++] = txOptions;
  buffer[idx++] = 0;
  buffer[idx++] = 0;
  buffer[idx++] = 0;
  buffer[idx++] = 0;
  buffer[idx++] = byCompletedFunc;      // Func id for CompletedFunc
  cbFuncZWSendDataBridge = completedFunc;
  if(SendFrameWithResponse(FUNC_ID_ZW_SEND_DATA_BRIDGE,buffer, idx , buffer, &byLen) != conFrameReceived) {
    buffer[IDX_DATA] = FALSE;
    SER_PRINTF("Fail\n");
  } else {
    txnr++;
  }

  if(buffer[IDX_DATA] != TRUE) {
    SER_PRINTF("SendData fail\n");
    cbFuncZWSendDataBridge = 0;
  }

  return buffer[IDX_DATA];
}


/**
 * \ingroup ZWCMD
 * Abort the ongoing transmit started with ZW_SendData() or ZW_SendDataMulti_Bridge(). If an ongoing
 * transmission is aborted, the callback function from the send call will return with the status
 * TRANSMIT_COMPLETE_NO_ACK.
 */
void ZW_SendDataAbort( void ){
  byLen = 0;
  SendFrame( FUNC_ID_ZW_SEND_DATA_ABORT,0, 0);
}
/*
 * \serialapi{
 * HOST->ZW: REQ | 0x57 | node | txOption | funcID
 * ZW->HOST: RES | 0x57 | RetVal
 * ZW->HOST: REQ | 0x57 | funcID | txStatus
 * }
 */
uint8_t ZW_SendSUCID(uint16_t node, uint8_t txOption,
                     VOID_CALLBACKFUNC(completedFunc)(BYTE txStatus,
                                       TX_STATUS_TYPE * txStatusReport))
{
  idx = 0;
  byCompletedFunc = (completedFunc== NULL ? 0 : 0x03);
  set_node_id_in_buffer(node);
  buffer[idx++] = txOption;
  buffer[idx++] = byCompletedFunc;
  cbFuncZWSendSUCID = completedFunc;
  SendFrameWithResponse(FUNC_ID_ZW_SEND_SUC_ID, buffer, idx , buffer, 0);
  return buffer[IDX_DATA];
}

/*
 * \serialapi{
 * HOST->ZW: REQ | 0x3C | bChannel | bThreshold
 * ZW->HOST: RES | 0x3C | TRUE
 * }
 * RET:
 * 0 - failed, 1 - success
 */
uint8_t ZW_SetListenBeforeTalkThreshold(uint8_t bChannel, uint8_t bThreshold) {
  idx = 0;
  buffer[idx++] = bChannel;
  buffer[idx++] = bThreshold;
  SendFrameWithResponse(FUNC_ID_ZW_SET_LISTEN_BEFORE_TALK_THRESHOLD, buffer, idx , buffer, 0);
  return buffer[IDX_DATA];
}
/**
  * \ingroup ZWCMD
  * Copy the Home-ID and Node-ID to the specified RAM addresses.
  * \param[out] homeID  Home-ID pointer (stored in network byte order/big endian)
  * \param[out] nodeID  Node-ID pointer (stored in host order)
  */
void MemoryGetID( BYTE *pHomeID, uint16_t *pNodeID )
{
    int j = 1;
    idx = 0;
    byLen = 0;
    SendFrameWithResponse( FUNC_ID_MEMORY_GET_ID,0, 0, buffer, &byLen );
    *pNodeID = 0;

    /* On the Z-Wave module the home id is stored as big endian. This is also how we get it here. */
    pHomeID[ 0 ] = buffer[ IDX_DATA ];
    pHomeID[ 1 ] = buffer[ IDX_DATA + (j++) ];
    pHomeID[ 2 ] = buffer[ IDX_DATA + (j++) ];
    pHomeID[ 3 ] = buffer[ IDX_DATA + (j++) ];
    if (lr_enabled  && (byLen == 8)) {
       WRN_PRINTF("Short frame for Long range enabled in FUNC_ID_MEMORY_GET_ID");
      *pNodeID = 0;
    } else {
      if (lr_enabled && (byLen == 9)) {
        *pNodeID = buffer[ IDX_DATA + (j++) ] << 8;
      }
      *pNodeID |= buffer[ IDX_DATA + (j++) ];
    }
    if((*pNodeID < 1) ||
       ((*pNodeID > ZW_CLASSIC_MAX_NODES) && (*pNodeID < ZW_LR_MIN_NODE_ID)) ||
       (*pNodeID > ZW_LR_MAX_NODE_ID)) {
      ERR_PRINTF("Module returns a bad node ID! Resetting module (%d)",*pNodeID);
      ZW_SetDefault(0);
      *pNodeID = 1;
    }
}

/**
 * \ingroup ZWCMD
 *  Read one byte from the EEPROM
 *  @param[in] offset Application area offset
 *  @param[in] pointer to byte where output should be copied 
 *  @retval 1 on success 0 on failure
 */
BYTE
MemoryGetByte(
  WORD offset, BYTE *byte)
{
  idx = 0;
  buffer[idx++] = (offset) >> 8;
  buffer[idx++] = (offset) & 0xFF;
  byLen = 0;
  if(SendFrameWithResponse(FUNC_ID_MEMORY_GET_BYTE,buffer, idx, buffer, &byLen)) {
    *byte = buffer[IDX_DATA];
  } else {
    return 0;
  }
  return 1;
}

/**
*  Add one byte to the EEPROM write queue
*  @param[in] Application area offset
*  @param[in] Data to store
*  @retval FALSE if write buffer full
*/
BYTE                    /*RET    */
MemoryPutByte(
  WORD  offset,
  BYTE  bData )
{
  idx = 0;
  buffer[idx++] = (offset) >> 8;
  buffer[idx++] = (offset) & 0xFF;
  buffer[idx++] = bData;
  byLen = 0;
  SendFrameWithResponse(FUNC_ID_MEMORY_PUT_BYTE,buffer, idx, buffer, &byLen);
  return buffer[IDX_DATA];
}

/**
  * \ingroup ZWCMD
  * Read number of bytes from the EEPROM to a RAM buffer.
  * \param[in] offset   Application area offset
  * \param[in] buffer   Buffer pointer
  * \param[in] length   Number of bytes to read
  *  @retval 1 on success 0 on failure
  */
BYTE MemoryGetBuffer( WORD offset, BYTE *buf, BYTE length )
{
    int i;

    if(SupportsCommand(FUNC_ID_MEMORY_GET_BUFFER)) {
      idx = 0;
      buffer[ idx++ ] = ( offset ) >> 8;
      buffer[ idx++ ] = ( offset ) & 0xFF;
      buffer[ idx++ ] = (BYTE)length;		// Number of bytes to read

      byLen = 0;
      if (SendFrameWithResponse(FUNC_ID_MEMORY_GET_BUFFER, buffer, idx, buffer, &byLen )) {
        for ( i = 0;i < length;i++ )
        {
            buf[ i ] = buffer[ IDX_DATA + i ];
        }
      } else {
        return 0;
      }
    } else {
      for(i=0; i < length; i++) {
        if(!MemoryGetByte(offset+i, &buf[i])) {
            return 0;
        }
      }
    }
    return 1;
}

/**
  * \ingroup ZWCMD
  * Copy number of bytes from a RAM buffer to the EEPROM.
  * \param[in] offset   Application area offset
  * \param[in] buffer   Buffer pointer
  * \param[in] length   Number of bytes to write
  * \param[in] func     Write completed function pointer
  * \return FALSE if the buffer put queue is full
  */
BYTE MemoryPutBuffer( WORD offset, BYTE *buf, WORD length, void ( *func ) ( void ) )
{
    int i;

    if(SupportsCommand(FUNC_ID_MEMORY_PUT_BUFFER)) {
      idx = 0;
      byCompletedFunc = ( func == NULL ? 0 : 0x03 );
      buffer[ idx++ ] = ( offset ) >> 8;
      buffer[ idx++ ] = ( offset ) & 0xFF;

      if ( length > BUF_SIZE - 8 )
          length = BUF_SIZE - 8;

      buffer[ idx++ ] = ( length ) >> 8;

      buffer[ idx++ ] = ( length ) & 0xFF;

      for ( i = 0; i < length; i++ )
      {
          buffer[ idx++ ] = buf[ i ];
      }

      buffer[ idx++ ] = byCompletedFunc;
      cbFuncMemoryPutBuffer = func;
      byLen = 0;
      SendFrameWithResponse(FUNC_ID_MEMORY_PUT_BUFFER, buffer, idx , buffer, &byLen );
      return buffer[ IDX_DATA ];
    } else {
      for(i=0; i < length; i++) {
        MemoryPutByte(offset+i, buf[i]);
        if(func) func();
      }
      return TRUE;
    }
}

uint8_t SerialAPI_nvm_close()
{
    idx = 0;
    buffer[idx++] = 0x03;
    SendFrameWithResponse(FUNC_ID_NVM_BACKUP_RESTORE, buffer, idx , buffer, 0);
    return buffer[IDX_DATA];
}

uint32_t SerialAPI_nvm_open()
{
    uint32_t len = 0;
    idx = 0;
    buffer[idx++] = 0x00;

    int status = SendFrameWithResponse(FUNC_ID_NVM_BACKUP_RESTORE, buffer, idx , buffer, 0);
    if(status != conTxErr && status != conIdle)
		{
			len = buffer[IDX_DATA+2] << 8;
			len = len | buffer[IDX_DATA+3];
		}
    //DBG_PRINTF("nvm_open says size is: %d(0x%x) bytes\n", len, len);
    return len;
}

/**
 * Read a chunk of the 500-series NVM or 700-series NVM3.
 *
 * \returns Status Code for the backup operation.
 *
 *  \retval 0 Status OK
 *  \retval 1 Unspecified Error
 *  \retval 2 - Status End-of-file
 */
uint8_t SerialAPI_nvm_backup(uint16_t offset, uint8_t *buf, uint8_t length, uint8_t *length_read)
{
  int i;

  //DBG_PRINTF("SupportsCommand FUNC_ID_NVM_BACKUP_RESTORE, offset: %d, buf: %p, length: %d\n", offset, buf, length);
  idx = 0;
  buffer[idx++] = 0x01; /* read */

  buffer[idx++] = length;

  buffer[idx++] = (offset) >> 8;
  buffer[idx++] = (offset) & 0xFF;

  SendFrameWithResponse(FUNC_ID_NVM_BACKUP_RESTORE, buffer, idx, buffer, 0);
  *length_read = buffer[IDX_DATA + 1];

  for (i = 0; i < *length_read; i++)
  {
    buf[i] = buffer[IDX_DATA + 4 + i];
  }
  switch (buffer[IDX_DATA])
  {
  case 0:
    return 0; /* Status Ok*/
  case 0xFF:
    return 2; /* Status End-of-file */
  default:
    ERR_PRINTF("SendFrameWithResponse failed in nvm_backup\n");
    return 1; /* Unspecified error  */
  }
}

uint8_t SerialAPI_nvm_restore(uint16_t offset, uint8_t *buf, uint8_t length, uint8_t *length_written)
{
  int i;

  //DBG_PRINTF("SupportsCommand FUNC_ID_NVM_BACKUP_RESTORE, offset: %d, buf: %p, length: %d\n", offset, buf, length);
  idx = 0;
  buffer[idx++] = 0x02; /* write */

  buffer[idx++] = length;

  buffer[idx++] = (offset) >> 8;
  buffer[idx++] = (offset) & 0xFF;

  for (i = 0; i < length; i++)
  {
    buffer[idx++] = buf[i];
  }
  SendFrameWithResponse(FUNC_ID_NVM_BACKUP_RESTORE, buffer, idx, buffer, 0);
  *length_written = buffer[IDX_DATA + 1];
  switch (buffer[IDX_DATA])
  {
  case 0:
    return 0;
  case 0xFF:
    return 2;
  default:
    ERR_PRINTF("SendFrameWithResponse failed in nvm_restore\n");
    return 1;
  }
}

/**
  * \ingroup ZWCMD
  * Copy number of bytes from a RAM buffer to the EEPROM.
  * \param[in] offset   Application area offset
  * \param[in] buffer   Buffer pointer
  * \param[in] length   Number of bytes to write
  * \param[in] func     Write completed function pointer
  * \return FALSE if the buffer put queue is full
  */
BYTE ZW_MemoryPutBuffer( WORD offset, BYTE *buf, WORD length)
{
    int i;

    if(SupportsCommand(FUNC_ID_MEMORY_PUT_BUFFER)) {
      idx = 0;

      buffer[ idx++ ] = ( offset ) >> 8;
      buffer[ idx++ ] = ( offset ) & 0xFF;

      if ( length > BUF_SIZE - 8 )
          length = BUF_SIZE - 8;

      buffer[ idx++ ] = ( length ) >> 8;

      buffer[ idx++ ] = ( length ) & 0xFF;

      for ( i = 0; i < length; i++ )
      {
          buffer[ idx++ ] = buf[ i ];
      }
      buffer[ idx++ ] = byCompletedFunc;

      byLen = 0;
      SendFrameWithResponse( FUNC_ID_MEMORY_PUT_BUFFER,buffer, idx, buffer, &byLen );

      return buffer[ IDX_DATA ];
    } else {
      for(i=0; i < length; i++) {
        MemoryPutByte(offset+i, buf[i]);
      }
      return TRUE;
    }
}







/**
  * \ingroup ZWCMD
  * Lock a response route for a specific node.
  * \param[in] bNodeID
  */
void ZW_LockRoute( BYTE bNodeID )
{
    idx = 0;
    buffer[ idx++ ] = bNodeID;
    SendFrame(FUNC_ID_LOCK_ROUTE_RESPONSE, buffer, idx );
}

/**
  * \ingroup ZWCMD
  * Read out neighbor information.
  * \param[in] bNodeID        Node ID on node whom routing info is needed on
  * \param[out] pMask            Pointer where routing info should be putd
  * \param[in]  bOptions     If TRUE remove bad repeaters
  * \param[in]  Upper nibble is bit flag options, lower nibble is speed
  *     Combine exactly one speed with any number of options
  *     Bit flags options for upper nibble:
  *       ZW_GET_ROUTING_INFO_REMOVE_BAD      - Remove bad link from routing info
  *       ZW_GET_ROUTING_INFO_REMOVE_NON_REPS  - Remove non-repeaters from the routing info
  *     Speed values for lower nibble:
  *       ZW_GET_ROUTING_INFO_ANY  - Return all nodes regardless of speed
  *       ZW_GET_ROUTING_INFO_9600 - Return nodes supporting 9.6k
  *       ZW_GET_ROUTING_INFO_40K  - Return nodes supporting 40k
  *       ZW_GET_ROUTING_INFO_100K - Return nodes supporting 100k
*/

void ZW_GetRoutingInfo_old(uint16_t bNodeID, BYTE *buf, BYTE bRemoveBad, BYTE bRemoveNonReps)
{
    int i;
    idx = 0;
    /*  bLine | bRemoveBad | bRemoveNonReps | funcID */
    set_node_id_in_buffer(bNodeID);
    buffer[ idx++ ] = bRemoveBad;
    buffer[ idx++ ] = bRemoveNonReps;
    byLen = 0;

    SendFrameWithResponse( FUNC_ID_GET_ROUTING_TABLE_LINE, buffer, idx, buffer, &byLen );

    for ( i = 0;i < 29;i++ )
    {
        buf[ i ] = buffer[ IDX_DATA + i ];
    }
}

/**
  * \ingroup ZWCMD
  * Reset TX Counter.
  */
void ZW_ResetTXCounter( void )
{
    idx = 0;
    SendFrame(FUNC_ID_RESET_TX_COUNTER, buffer, idx );
}

/**
  * \ingroup ZWCMD
  * Get TX Counter.
  * \return The number of frames transmitted since the TX Counter last was reset
  */
BYTE ZW_GetTXCounter( void )
{
    idx = 0;
    byLen = 0;
    SendFrameWithResponse( FUNC_ID_GET_TX_COUNTER, buffer, idx, buffer, &byLen );
    return buffer[ IDX_DATA ];
}


/**
 * \ingroup ZWCMD
 * Start neighbor discovery for bNodeID
 * 
 * \param[in] bNodeID ID of node the controller wants to get neighbors for.
 * \param[in] completedFunc Callback function
 *
 * \return 1 allways
 */
BYTE
ZW_RequestNodeNeighborUpdate(
  uint16_t bNodeID,                     /* IN Node id */
  VOID_CALLBACKFUNC(completedFunc)( /* IN Function to be called when the done */
    auto BYTE))
{
    idx = 0;
    byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
    set_node_id_in_buffer(bNodeID);
    buffer[ idx++ ] = byCompletedFunc;
    cbFuncZWRequestNodeNodeNeighborUpdate = completedFunc;
    SendFrame(FUNC_ID_ZW_REQUEST_NODE_NEIGHBOR_UPDATE, buffer, idx);
    return 1;
}

/**
  * \ingroup ZWCMD
  * Copy the Node's current protocol information from the non-volatile memory.
  * \param[in] nodeID      Node ID
  * \param[out] nodeInfo   Node info buffer
  */

void ZW_GetNodeProtocolInfo( uint16_t bNodeID, NODEINFO *nodeInfo )
{
    idx = 0;
    set_node_id_in_buffer(bNodeID);
    byLen = 0;
    SendFrameWithResponse( FUNC_ID_ZW_GET_NODE_PROTOCOL_INFO, buffer, idx, buffer, &byLen );

    nodeInfo->capability = buffer[ IDX_DATA ];
    nodeInfo->security = buffer[ IDX_DATA + 1 ];
    nodeInfo->reserved = buffer[ IDX_DATA + 2 ];
#ifdef NEW_NODEINFO

    nodeInfo->nodeType.basic = buffer[ IDX_DATA + 3 ];
    nodeInfo->nodeType.generic = buffer[ IDX_DATA + 4 ];
    nodeInfo->nodeType.specific = buffer[ IDX_DATA + 5 ];
#else

    nodeInfo->nodeType = buffer[ IDX_DATA + 3 ];
#endif
}

void ZW_GetVirtualNodes(char *pNodeMask)
{
    idx = 0;
    byLen = 0;
    SendFrameWithResponse( FUNC_ID_ZW_GET_VIRTUAL_NODES, buffer, idx, buffer, &byLen );
    memcpy(pNodeMask, buffer + IDX_DATA, MAX_CLASSIC_NODEMASK_LENGTH);
}


/*===========================   ZW_SetDefault   =============================
**    Remove all Nodes and timers from the EEPROM memory.
**
**--------------------------------------------------------------------------*/
void                                /*RET Nothing */
ZW_SetDefault(
  VOID_CALLBACKFUNC(completedFunc)( /* IN Command completed call back function */
    void))
{
    idx = 0;
    byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
    buffer[ idx++ ] = byCompletedFunc;
    cbFuncZWSetDefault = completedFunc;
    SendFrame(FUNC_ID_ZW_SET_DEFAULT, buffer, idx );
}

/*========================   ZW_ControllerChange   ======================
**
**    Transfer the role as primary controller to another controller
**
**    The modes are:
**
**    CONTROLLER_CHANGE_START          Start the creation of a new primary
**    CONTROLLER_CHANGE_STOP           Stop the creation of a new primary
**    CONTROLLER_CHANGE_STOP_FAILED    Report that the replication failed
**
**    ADD_NODE_OPTION_HIGH_POWER       Set this flag in bMode for High Power exchange.
**
**    Side effects:
**
**--------------------------------------------------------------------------*/
void
ZW_ControllerChange(BYTE bMode,
                        VOID_CALLBACKFUNC(completedFunc)(auto LEARN_INFO*))
{
    idx = 0;
    byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
    buffer[ idx++ ] = bMode;
    buffer[ idx++ ] = byCompletedFunc;
    cbFuncZWControllerChange = completedFunc;
    SendFrame(FUNC_ID_ZW_CONTROLLER_CHANGE, buffer, idx );
}


/*========================   ZW_CreateNewPrimaryCtrl   ======================
**
**    Create a new primary controller
**
**    The modes are:
**
**    CREATE_PRIMARY_START          Start the creation of a new primary
**    CREATE_PRIMARY_STOP           Stop the creation of a new primary
**    CREATE_PRIMARY_STOP_FAILED    Report that the replication failed
**
**    ADD_NODE_OPTION_HIGH_POWER    Set this flag in bMode for High Power inclusion.
**
**    Side effects:
**
**--------------------------------------------------------------------------*/
void
ZW_CreateNewPrimaryCtrl(BYTE bMode,
                        VOID_CALLBACKFUNC(completedFunc)(LEARN_INFO*)) {
  idx = 0;
  byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
  buffer[ idx++ ] = bMode;
  buffer[ idx++ ] = byCompletedFunc;
  cbFuncZWNewController= completedFunc;
  SendFrame(FUNC_ID_ZW_NEW_CONTROLLER, buffer, idx );
}

/*==========================   ZW_AddNodeToNetwork   ========================
**
**    Add any type of node to the network
**
**    The modes are:
**
**    ADD_NODE_ANY            Add any node to the network
**    ADD_NODE_CONTROLLER     Add a controller to the network
**    ADD_NODE_SLAVE          Add a slaev node to the network
**    ADD_NODE_STOP           Stop learn mode without reporting an error.
**    ADD_NODE_STOP_FAILED    Stop learn mode and report an error to the
**                            new controller.
**
**    ADD_NODE_OPTION_HIGH_POWER
**    ADD_NODE_OPTION_LR            Set this flag in bMode to include a node in the Long range mode.
**    ADD_NODE_OPTION_SFLND         Set this flag in bMode if the Add node operation
**                                    must skip FL nodes in neighbors discovery
**                                         of the included node
*    Set this flag in bMode for High Power inclusion.
**
**    Side effects:
**
**--------------------------------------------------------------------------*/
void
ZW_AddNodeToNetwork(BYTE bMode,
                    VOID_CALLBACKFUNC(completedFunc)(auto LEARN_INFO*))
{
    idx = 0;
    byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
    buffer[ idx++ ] = bMode;
    buffer[ idx++ ] = byCompletedFunc;
    cbFuncAddNodeToNetwork = completedFunc;
    SendFrame(FUNC_ID_ZW_ADD_NODE_TO_NETWORK, buffer, idx );
}

/*==========================   ZW_RemoveNodeFromNetwork   ========================
**
**    Remove any type of node from the network
**
**    The modes are:
**
**    REMOVE_NODE_ANY            Remove any node from the network
**    REMOVE_NODE_CONTROLLER     Remove a controller from the network
**    REMOVE_NODE_SLAVE          Remove a slaev node from the network
**
**    REMOVE_NODE_STOP           Stop learn mode without reporting an error.
**
**    ADD_NODE_OPTION_HIGH_POWER    Set this flag in bMode for High Power exclusion.
**
**    Side effects:
**
**--------------------------------------------------------------------------*/
void
ZW_RemoveNodeFromNetwork(BYTE bMode,
                    VOID_CALLBACKFUNC(completedFunc)(auto LEARN_INFO*))
{
    idx = 0;
    byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
    buffer[ idx++ ] = bMode;
    buffer[ idx++ ] = byCompletedFunc;
    cbFuncRemoveNodeFromNetwork = completedFunc;
    SendFrame(FUNC_ID_ZW_REMOVE_NODE_FROM_NETWORK, buffer, idx );
}

/*==========================   ZW_RemoveFailedNode   ===============================
**
**    remove a node from the failed node list, if it already exist.
**    A call back function should be provided otherwise the function will return
**    without removing the node.
**    If the removing process started successfully then the function will return
**    ZW_FAILED_NODE_REMOVE_STARTED        The removing process started
**
**    If the removing process can not be started then the API function will return
**    on or more of the following flags
**    ZW_NOT_PRIMARY_CONTROLLER             The removing process was aborted because the controller is not the primaray one
**    ZW_NO_CALLBACK_FUNCTION              The removing process was aborted because no call back function is used
**    ZW_FAILED_NODE_NOT_FOUND             The removing process aborted because the node was node found
**    ZW_FAILED_NODE_REMOVE_PROCESS_BUSY   The removing process is busy
**
**    The call back function parameter value is:
**
**    ZW_NODE_OK                     The node is working proppely (removed from the failed nodes list )
**    ZW_FAILED_NODE_REMOVED         The failed node was removed from the failed nodes list
**    ZW_FAILED_NODE_NOT_REMOVED     The failed node was not
**    Side effects:
**--------------------------------------------------------------------------*/
BYTE                                /*RET function return code */
ZW_RemoveFailedNode(
  uint16_t NodeID,                      /* IN the failed nodeID */
  VOID_CALLBACKFUNC(completedFunc)( /* IN callback function to be called */
    BYTE)) {                         /*    when the remove process end. */

  idx = 0;
  byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
  set_node_id_in_buffer(NodeID);
  buffer[ idx++ ] = byCompletedFunc;
  cbFuncZWRemoveFailedNode = completedFunc;

  byLen = 0;
  SendFrameWithResponse(FUNC_ID_ZW_REMOVE_FAILED_NODE_ID, buffer, idx, buffer, &byLen );
  return buffer[ IDX_DATA ];
}


BYTE                                /*RET function return code */
ZW_ReplaceFailedNode(uint16_t NodeID,
    BOOL bNormalPower,
VOID_CALLBACKFUNC(completedFunc)(BYTE txStatus))
{
  idx = 0;
  byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
  set_node_id_in_buffer(NodeID);
  buffer[ idx++ ] = byCompletedFunc;
  cbFuncZWReplaceFailedNode = completedFunc;

  byLen = 0;
  SendFrameWithResponse(FUNC_ID_ZW_REPLACE_FAILED_NODE, buffer, idx, buffer, &byLen );
  return buffer[ IDX_DATA ];
}




/**
  * \ingroup ZWCMD
  * Sends command completed to master remote.
  * Called in replication mode when a command from the sender has been processed.
  */
/*
void ZW_ReplicationCommandComplete( void )
{
    idx = 0;
    buffer[ idx++ ] = 0;
    buffer[ idx++ ] = REQUEST;
    buffer[ idx++ ] = FUNC_ID_ZW_REPLICATION_COMMAND_COMPLETE;
    buffer[ 0 ] = idx;	// length
    SendData( buffer, idx );
}
*/

/**
  * \ingroup ZWCMD
  * Used when the controller is replication mode.
  * It sends the payload and expects the receiver to respond with a command complete message.
  * \param[in] nodeID         Destination node ID
  * \param[in] pData          Data buffer pointer
  * \param[in] dataLength     Data buffer length
  * \param[in] txOptions      Transmit option flags
  * \param[in] completedFunc  Transmit completed call back function
  * \param[in] txStatus       Transmit status
  * \return FALSE if transmitter queue overflow
  */
/*
BYTE ZW_ReplicationSendData( BYTE nodeID, BYTE *pData, BYTE dataLength, BYTE txOptions, void ( *completedFunc ) ( BYTE ) )
{
    int i;
    idx = 0;
    BYTE byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
    buffer[ idx++ ] = nodeID;
    buffer[ idx++ ] = dataLength;

    for ( i = 0;i < dataLength;i++ )
        buffer[ idx++ ] = pData[ i ];

    buffer[ idx++ ] = txOptions;
    buffer[ idx++ ] = byCompletedFunc;	// Func id for CompletedFunc
    byLen = 0;
    cbFuncZWReplicationSendData = completedFunc;
    SendDataAndWaitForResponse( buffer, idx, FUNC_ID_ZW_REPLICATION_SEND_DATA, buffer, &byLen );

    return buffer[ IDX_DATA ];
}
*/

/*========================   ZW_AssignReturnRoute   =========================
**
**    Assign static return routes within a Routing Slave node.
**    Calculate the shortest transport routes from the Routing Slave node
**    to the route destination node and
**    transmit the return routes to the Routing Slave node.
**
**--------------------------------------------------------------------------*/
BOOL                                /*RET TRUE if assign was initiated. FALSE if not */
ZW_AssignReturnRoute(
  uint16_t bSrcNodeID,                 /* IN Routing Slave Node ID */
  uint16_t bDstNodeID,                 /* IN Route destination Node ID */
  VOID_CALLBACKFUNC(completedFunc)( /* IN Callback function called when done */
  BYTE bStatus))
{
    idx = 0;
    byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
    set_node_id_in_buffer(bSrcNodeID);
    set_node_id_in_buffer(bDstNodeID);
    buffer[ idx++ ] = byCompletedFunc;	// Func id for CompletedFunc
    cbFuncZWAssignReturnRoute = completedFunc;

    byLen = 0;
    SendFrameWithResponse(FUNC_ID_ZW_ASSIGN_RETURN_ROUTE, buffer, idx, buffer, &byLen );
    return buffer[ IDX_DATA ];

///    SendData( buffer, idx );
///    return  0;
}

/**
  * \ingroup ZWCMD
  * Delete static return routes within a Routing Slave node.
  * Transmit "NULL" routes to the Routing Slave node.
  * \param[in] nodeID          Routing Slave
  * \param[in] completedFunc   Completion handler
  * \param[in] bStatus        Transmit complete status
  */

BOOL ZW_DeleteReturnRoute( uint16_t nodeID, void ( *completedFunc ) ( BYTE ) )
{
    BYTE byCompletedFunc;

    idx = 0;
    byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
    set_node_id_in_buffer(nodeID);
    buffer[ idx++ ] = byCompletedFunc;	// Func id for CompletedFunc

    cbFuncZWDeleteReturnRoute = completedFunc;

    SendFrameWithResponse(FUNC_ID_ZW_DELETE_RETURN_ROUTE, buffer, idx, buffer, &byLen );
    return buffer[ IDX_DATA ];
}


/**
  * \ingroup ZWCMD
  * Assign static return routes within a Routing Slave node.
  * Calculate the shortest transport routes to a Routing Slave node
  * from the Static Update Controller (SUC) Node and
  * transmit the return routes to the Routing Slave node.
  * \param[in] bSrcNodeID     Routing Slave Node ID
  * \param[in] completedFunc  Completion handler
  * \param[in] bStatus        Transmit complete status
  */

BOOL ZW_AssignSUCReturnRoute( uint16_t bSrcNodeID, void ( *completedFunc ) ( BYTE ) )
{
    BYTE byCompletedFunc;

    idx = 0;
    byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );

    set_node_id_in_buffer(bSrcNodeID);
    buffer[ idx++ ] = byCompletedFunc;	// Func id for CompletedFunc
    cbFuncZWAssignSUCReturnRoute = completedFunc;
    SendFrameWithResponse(FUNC_ID_ZW_ASSIGN_SUC_RETURN_ROUTE,buffer,idx,buffer,&byLen);
    return buffer[ IDX_DATA ];
}


/**
  * \ingroup ZWCMD
  * Delete the ( Static Update Controller -SUC-) static return routes
  * within a Routing Slave node.
  * Transmit "NULL" routes to the Routing Slave node.
  * \param[in] nodeID         Routing Slave
  * \param[in] completedFunc  Completion handler
  * \param[in] bStatus        Transmit complete status
  */

BOOL ZW_DeleteSUCReturnRoute( uint16_t nodeID, void ( *completedFunc ) ( BYTE ) )
{
    BYTE byCompletedFunc;

    idx = 0;
    byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
    set_node_id_in_buffer(nodeID);
    buffer[ idx++ ] = byCompletedFunc;	// Func id for CompletedFunc
    cbFuncZWDeleteSUCReturnRoute = completedFunc;

    SendFrameWithResponse(FUNC_ID_ZW_DELETE_SUC_RETURN_ROUTE,buffer,idx,buffer,&byLen);
    return buffer[ IDX_DATA ];
}


/**
  * \ingroup ZWCMD
  * Get the currently registered SUC node ID.
  * \return SUC node ID, ZERO if no SUC available
  */
uint16_t ZW_GetSUCNodeID( void )
{
    idx = 0;
    byLen = 0;
    int j = 0;
    uint16_t suc_node_id = 0;
    SendFrameWithResponse(FUNC_ID_ZW_GET_SUC_NODE_ID,0, 0, buffer, &byLen );
    if (lr_enabled) {
      suc_node_id = buffer[ IDX_DATA + (j++)] << 8;
    }
    suc_node_id |= buffer[ IDX_DATA + j];

    return suc_node_id; 
}

/*============================   ZW_SetSUCNodeID  ===========================
**    Function description
**    This function enable /disable a specified static controller
**    of functioning as the Static Update Controller
**
**--------------------------------------------------------------------------*/
BYTE                 /*RET TRUE target is a static controller*/
                     /*    FALSE if the target is not a static controller,  */
                     /*    the source is not primary or the SUC functinality is not enabled.*/
ZW_SetSUCNodeID(
  uint16_t nodeID,       /* IN the node ID of the static controller to be a SUC */
  BYTE SUCState,     /* IN TRUE enable SUC, FALSE disable */
  BYTE bTxOption,    /* IN TRUE if to use low poer transmition, FALSE for normal Tx power */
  BYTE bCapabilities,             /* The capabilities of the new SUC */
  VOID_CALLBACKFUNC(completedFunc)(auto BYTE txStatus)) /* IN a call back function */
{
    idx = 0;
    byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
    set_node_id_in_buffer(nodeID);
    buffer[ idx++ ] = SUCState;  /* Do we want to enable or disable?? */
    buffer[ idx++ ] = bTxOption;
    buffer[ idx++ ] = bCapabilities;
    buffer[ idx++ ] = byCompletedFunc;	// Func id for CompletedFunc

    cbFuncZWSetSUCNodeID = completedFunc;
    byLen = 0;
    SendFrameWithResponse(FUNC_ID_ZW_SET_SUC_NODE_ID, buffer, idx, buffer, &byLen );
    return buffer[ IDX_DATA ];
}

/*===========================   ZW_SetLearnMode   ===========================
**    Enable/Disable home/node ID learn mode.
**    When learn mode is enabled, received "Assign ID's Command" are handled:
**    If the current stored ID's are zero, the received ID's will be stored.
**    If the received ID's are zero the stored ID's will be set to zero.
**
**    The learnFunc is called when the received assign command has been handled.
**
**--------------------------------------------------------------------------*/
void                                    /*RET  Nothing        */
ZW_SetLearnMode(
  BYTE mode,                            /*IN  TRUE: Enable; FALSE: Disable   */
  VOID_CALLBACKFUNC(completedFunc)(LEARN_INFO*)/*VOID_CALLBACKFUNC(learnFunc)( BYTE bStatus,  BYTE nodeID)*/)  /*IN  Node learn call back function. */
{
  idx = 0;
  byCompletedFunc = (completedFunc == NULL ? 0 : 0x03);
  buffer[idx++] = mode;
  buffer[idx++] = byCompletedFunc;      // Func id for CompletedFunc
  cbFuncZWSetLearnMode = completedFunc;
  SendFrame(FUNC_ID_ZW_SET_LEARN_MODE,buffer, idx);
}

/**
  * \ingroup ZWCMD
  * Get the Z-Wave library basis version.
  * \param[out]   pBuf
  *   Pointer to buffer where version string will be
  *   copied to. Buffer must be at least 14 bytes long. If NULL, nothing will be copied.
  */
BYTE ZW_Version( BYTE *pBuf )
{
    BYTE retVal;

    idx = 0;
    byLen = 0;
    retVal = SendFrameWithResponse(FUNC_ID_ZW_GET_VERSION,0 , 0, buffer, &byLen );
    if(retVal==conFrameReceived) {
        if (pBuf)
        {
          memcpy( pBuf, (void *)&buffer[ IDX_DATA ], 12 );
        }
        return buffer[ IDX_DATA + 12 ];
    }
    return 0;
}
/**
  * \ingroup ZWCMD
  * Get the Z-Wave protocol version.
  * \param[out]   pBuf
  *   Pointer to buffer where version string will be
  *   copied to. Buffer must be of PROTOCOL_VERSION size. If NULL, nothing will be copied.
  */

void ZW_GetProtocolVersion(PROTOCOL_VERSION *pBuf)
{
    BYTE retVal;

    idx = 0;
    byLen = 0;
    retVal = SendFrameWithResponse(FUNC_ID_ZW_GET_PROTOCOL_VERSION, 0, 0, buffer, &byLen );
    if(retVal == conFrameReceived) {
        if (pBuf)
        {
            /*
             * FUNC_ID_ZW_GET_PROTOCOL_VERSION should return
             * sizeof(PROTOCOL_VERSION) + IDX_DATA = 25 bytes if it returns more
             * copy only 25
             *
             * If it returns less copy whatever returned
             * Note: Older protocols (500 series) might return smaller byLen
             * than 25
             */ 
          switch (byLen - IDX_DATA) {
            case (sizeof (PROTOCOL_VERSION)):
              memcpy(pBuf->git_hash_id, (void *)&buffer[IDX_DATA+6], 16);
              //No break is intentional
            case 6:
              pBuf->zaf_build_no = buffer[IDX_DATA+4] << 8; 
              pBuf->zaf_build_no |= buffer[IDX_DATA+5];
              //No break is intentional
            case 4:
              pBuf->protocolType = buffer[IDX_DATA];
              pBuf->protocolVersionMajor = buffer[IDX_DATA+1];
              pBuf->protocolVersionMinor = buffer[IDX_DATA+2];
              pBuf->protocolVersionRevision = buffer[IDX_DATA+3];
              break;
            default:
              SER_PRINTF("ZW_GetProtocolVersion returned %d bytes", (byLen - IDX_DATA));
          }
        }
    }
    return;
}

BYTE ZW_SetSlaveLearnMode(uint8_t node, BYTE mode, /*IN */
                          VOID_CALLBACKFUNC(completedFunc)(BYTE, uint16_t,
                                                           uint16_t)) {
  BYTE retVal;

  idx = 0;
  byCompletedFunc = (completedFunc == NULL ? 0 : 0x03);
  buffer[idx++] = node;
  buffer[idx++] = mode;
  buffer[idx++] = byCompletedFunc;      // Func id for CompletedFunc
  cbFuncZWSetSlaveLearnMode = completedFunc;
  retVal = SendFrameWithResponse(FUNC_ID_ZW_SET_SLAVE_LEARN_MODE, buffer, idx, buffer, &byLen );
  if (retVal == conFrameReceived) {
    return buffer[ IDX_DATA ];
  }
  return 0;
}

/**
  * \ingroup ZWCMD
  * Set ApplicationNodeInformation data to be used in subsequent
  * calls to \ref ZW_SendNodeInformation.
  * \param[in] listening   = TRUE : if this node is always on air
  * \param[in] nodeType    Device type
  * \param[in] nodeParm    Device parameter buffer
  * \param[in] parmLength  Number of Device parameter bytes
  */
void SerialAPI_ApplicationNodeInformation( BYTE listening, APPL_NODE_TYPE nodeType, BYTE *nodeParm, BYTE parmLength )
{
    int i;
    idx = 0;
    buffer[ idx++ ] = listening;
    buffer[ idx++ ] = nodeType.generic;
    buffer[ idx++ ] = nodeType.specific;
    buffer[ idx++ ] = parmLength;

    for ( i = 0; i != parmLength; i++ )
    {
        buffer[ idx++ ] = nodeParm[ i ];
    }
    SendFrame(FUNC_ID_SERIAL_API_APPL_NODE_INFORMATION, buffer, idx );
}

static bool is_nodeid_basetype_8()
{
    BYTE buff[9]; /* FUNC_ID_MEMORY_GET_ID returns max 9 bytes*/ 
    idx = 0;
    byLen = 0;
    SendFrameWithResponse( FUNC_ID_MEMORY_GET_ID,0, 0, buff, &byLen );
    if (byLen == 8) {
      return true;
    } else {
      return false;
    }
}
/**
 * @brief Sets the nodeid type for the serial api
 * 
 * @param type 1: 8 bit node ids, 2 16bit nodeids
 * @return node_id_type_t indicates the node ID type being set
 */
static node_id_type_t SerialAPI_Setup_NodeID_BaseType_Set(node_id_type_t nodeid_basetype) {
  idx = 0;
  byLen = 0;
  buffer[idx++] = SERIAL_API_SETUP_CMD_NODEID_BASETYPE_SET;
  buffer[idx++] = nodeid_basetype;
  SendFrameWithResponse(FUNC_ID_SERIALAPI_SETUP, buffer, idx, buffer, &byLen);
  /*
   * 1 for success
   * 0 for error, in which case the host can assume node id type 8 bit
   */
  uint8_t success = buffer[IDX_DATA + 1];
  if (success) {
    SER_PRINTF("Setting Node ID basetype to %02x\n", nodeid_basetype);
    if (nodeid_basetype == NODEID_16BITS) {
      lr_enabled = 1;
    }
    return nodeid_basetype;
  }
  SER_PRINTF("Fail at setting Node ID basetype to %02x. Default set to 8bit.\n", nodeid_basetype);
  // When error, it's set to default 8 bit
  return NODEID_8BITS;
}

static void SerialAPI_LR_Virtual_Nodes_Set(uint8_t lr_virtual_nodes_bits) {
  /*
   * The last 4 bits indicate 4 virtual nodes in LR
   * 0 bit for 4002
   * 1 bit for 4003
   * 2 bit for 4004
   * 3 bit for 4005
   */
  buffer[0] = lr_virtual_nodes_bits;
  SendFrame(FUNC_ID_ZW_SET_LR_VIRTUAL_IDS, buffer, 1);
  SER_PRINTF("Setting Z-Wave Long Range virtual nodes to %02x\n", lr_virtual_nodes_bits);
}

/**
 * Enables LR mode.
 * Returns false if region is not set to US_LR or if basetype_set fails for some reason.
 * Returns true otherwise.
 */
bool SerialAPI_EnableLR() {
  if (SerialAPI_SupportsLR()) {
    if (NODEID_16BITS !=
        SerialAPI_Setup_NodeID_BaseType_Set(NODEID_16BITS)) {
      return false;
    }
    // 0x0f to enable all virtual nodes
    SerialAPI_LR_Virtual_Nodes_Set(0x0f);
  }
  return true;
}

bool SerialAPI_DisableLR() {
  if (SerialAPI_SupportsLR()) {
    if (NODEID_8BITS !=
        SerialAPI_Setup_NodeID_BaseType_Set(NODEID_8BITS)) {
      return false;
    }
    // 0x00 to disable all virtual nodes
    SerialAPI_LR_Virtual_Nodes_Set(0x00);
  }
  return true;
}

/**
  * \ingroup ZWCMD
  * Set ApplicationNodeInformation data to be used in subsequent
  * calls to \ref ZW_SendSlaveNodeInformation.
  * This takes effect for all virtual nodes, regardless of dstNode value.
  * \param[in] dstNode     Virtual node id. This value is IGNORED.
  * \param[in] listening   = TRUE : if this node is always on air
  * \param[in] nodeType    Device type
  * \param[in] nodeParm    Device parameter buffer
  * \param[in] parmLength  Number of Device parameter bytes
  */
void SerialAPI_ApplicationSlaveNodeInformation(uint8_t dstNode, BYTE listening,
                                               APPL_NODE_TYPE nodeType,
                                               BYTE *nodeParm,
                                               BYTE parmLength) {
  int i;
  idx = 0;
  buffer[idx++] = dstNode;
  buffer[ idx++ ] = listening;
  buffer[ idx++ ] = nodeType.generic;
  buffer[ idx++ ] = nodeType.specific;
  buffer[ idx++ ] = parmLength;

  for ( i = 0; i != parmLength; i++ )
  {
      buffer[ idx++ ] = nodeParm[ i ];
  }
  SendFrame(FUNC_ID_SERIAL_API_APPL_SLAVE_NODE_INFORMATION, buffer, idx );
}
 /*
  * \param[out]   len            Number of bytes in nodesList
  * \param[out]   nodesList      Bitmask list with nodes known by Z-Wave module
  */
void SerialAPI_GetLRNodeList(uint16_t *len, BYTE *lr_nodelist)
{
  BYTE *p;
  int i, bitmask_offset = 0;
  int boff = 0;
  int more_nodes = 1;

  *len = 0;

  if (!lr_enabled) {
    return;
  }
  /*
   * #define FUNC_ID_SERIAL_API_GET_LR_NODES 0xDA
   * REQ | 0xDA | BITMASK_OFFSET
   * RES | 0xDA | MORE_NODES | BITMASK_OFFSET | BITMASK_LEN | BITMASK_ARRAY
   *
   * BITMASK_OFFSET is 8 bit value and should be one of the following values 0, 1, 2, 3
   * MORE_NODES 1 byte if it 1 then more nodes available and the host can request the next chunk of the nodes bitmask array
   * BITMASK_LEN is a byte. Max is 128 bytes.
   * BITMASK_ARRAY is an array of 128 bytes contains a bitmask of the available nodes in the network.
   *
   * Bit n in bitmask byte j represents node ID n + (j * 8) + BITMASK_OFFET.
   */
  while (more_nodes) {
    buffer[0] = bitmask_offset;
    SendFrameWithResponse(FUNC_ID_SERIAL_API_GET_LR_NODES, buffer, 1, buffer, &byLen);
    if (buffer[IDX_CMD] != FUNC_ID_SERIAL_API_GET_LR_NODES) {
        assert(0);
    }
    more_nodes = buffer[IDX_DATA];
    boff = buffer[IDX_DATA+1];
    *len += buffer[ IDX_DATA + 2];
    //SER_PRINTF("Long Range node bitmap: length: %d, more_nodes:%d bitmask_offset: %d boff: %d\n",
    //           *len, more_nodes, bitmask_offset, boff);
    p =  &buffer[ IDX_DATA + 3];

    for (i = 0; i < *len; i++ ){
      //SER_PRINTF("%02x\n", *p);
      if ( (i + (bitmask_offset * 128)) > ((ZW_LR_MAX_NODE_ID - ZW_LR_MIN_NODE_ID)/8) ) {
        ASSERT(0);
        return;
      }
      lr_nodelist[i + ((bitmask_offset) * 128)] = *p++;
    }
    bitmask_offset++;
  }
}


uint8_t GetLongRangeChannel(void)
{
  idx = 0;
  byLen = 0;

  if(SupportsCommand(FUNC_ID_GET_LR_CHANNEL)) {
    SendFrameWithResponse(FUNC_ID_GET_LR_CHANNEL, buffer, idx, buffer, &byLen);
    return buffer[IDX_DATA];
  } else {
    SER_PRINTF("FUNC_ID_GET_LR_CHANNEL is NOT supported by Serial API\n");
    return LR_NOT_SUPPORTED;
  }

}

void SetLongRangeChannel(uint8_t channel)
{
  idx = 0;
  byLen = 0;
  buffer[idx++] = channel;
  if(SupportsCommand(FUNC_ID_SET_LR_CHANNEL)) {
    SendFrame(FUNC_ID_SET_LR_CHANNEL, buffer, idx);
  } else {
    SER_PRINTF("FUNC_ID_SET_LR_CHANNEL is NOT supported by Serial API\n");
  }
  return;
}

/**
  * \ingroup ZWCMD
  * Get Serial API initialization data from remote side (Enhanced Z-Wave module).
  * \param[out]   ver            Remote sides Serial API version
  * \param[out]   capabilities   Capabilities flag (GET_INIT_DATA_FLAG_xxx)
  *   Capabilities flag: \n
  *      Bit 0: 0 = Controller API; 1 = Slave API \n
  *      Bit 1: 0 = Timer functions not supported; 1 = Timer functions supported. \n
  *      Bit 2: 0 = Primary Controller; 1 = Secondary Controller \n
  *      Bit 3: 0 = Not SUC; 1 = This node is SUC (static controller only) \n
  *      Bit 3-7: Reserved \n
  *   Timer functions are: TimerStart, TimerRestart and TimerCancel.
  * \param[out]   len            Number of bytes in nodesList
  * \param[out]   nodesList      Bitmask list with nodes known by Z-Wave module
  */
BYTE SerialAPI_GetInitData( BYTE *ver, BYTE *capabilities, BYTE *len, BYTE *nodesList,BYTE* chip_type,BYTE* chip_version )
{
    BYTE *p;
    int i;
    idx = 0;
    byLen = 0;
    *ver = 0;
    *capabilities = 0;
    SendFrameWithResponse(FUNC_ID_SERIAL_API_GET_INIT_DATA, 0, 0, buffer, &byLen );
    p = &buffer[ IDX_DATA ];
    *ver = *p++;

    //controller api or slave api
    *capabilities = *p++;
    *len = *p++;

    for ( i = 0; i < 29; i++ )
        nodesList[ i ] = *p++;

    *chip_type=*p++;
    *chip_version=*p++;

    my_chip_data.chip_type = *chip_type;
    my_chip_data.chip_version = *chip_version;

    // Bit 2 tells if it is Primary Controller (FALSE) or Secondary Controller (TRUE).
    if ( ( *capabilities ) & GET_INIT_DATA_FLAG_SECONDARY_CTRL )
        return ( TRUE );
    else
        return ( FALSE );
}

/**
  * \ingroup ZWCMD
  * Enable the SUC functionality in a controller.
  * \param[in] state
  *   = TRUE : Enable SUC \n
  *   = FALSE : Disable SUC \n
  * \param[in] capabilities
  *   = ZW_SUC_FUNC_BASIC_SUC : Only enables the basic SUC functionality \n
  *   = ZW_SUC_FUNC_NODEID_SERVER : Enable the node ID server functionality to become a SIS. \n
  * \return
  *   = TRUE : The SUC functionality was enabled/disabled \n
  *   = FALSE : Attempting to disable a running SUC, not allowed \n
  */
BOOL ZW_EnableSUC( BYTE state, BYTE capabilities )
{
    idx = 0;
    idx++;
    buffer[ idx++ ] = state;
    buffer[ idx++ ] = capabilities;
    byLen = 0;
    SendFrameWithResponse( FUNC_ID_ZW_ENABLE_SUC, buffer,idx , buffer, &byLen );

    return buffer[ IDX_DATA ];
}

/*============================   ZW_RequestNodeInfo   ======================
**    Function description.
**     Request a node to send it's node information.
**     Function return TRUE if the request is send, else it return FALSE.
**     FUNC is a callback function, which is called with the status of the
**     Request nodeinformation frame transmission.
**     If a node sends its node info, ApplicationControllerUpdate will be called
**     with UPDATE_STATE_NODE_INFO_RECEIVED as status together with the received
**     nodeinformation.
**
**    Side effects:
**
**--------------------------------------------------------------------------*/
BOOL                      /*RET FALSE if transmitter busy */
ZW_RequestNodeInfo(
  uint16_t nodeID,                     /*IN: node id of the node to request node info from it.*/
  VOID_CALLBACKFUNC(completedFunc)(auto BYTE)) /* IN Callback function */
{
    idx = 0;
    byLen = 0;
    set_node_id_in_buffer(nodeID);
    SendFrameWithResponse(FUNC_ID_ZW_REQUEST_NODE_INFO, buffer, idx, buffer, &byLen );

    return buffer[ IDX_DATA ];
}

/**
  * \ingroup ZWCMD
  * Used when the controller is replication mode.
  * It sends the payload and expects the receiver to respond with a command complete message.
  * \param[in] nodeID         Destination node ID
  * \param[in] pData          Data buffer pointer
  * \param[in] dataLength     Data buffer length
  * \param[in] txOptions      Transmit option flags
  * \param[in] completedFunc  Transmit completed call back function
  * \param[in] txStatus       Transmit status
  * \return FALSE if transmitter queue overflow
  */

BYTE ZW_ReplicationSend( uint16_t nodeID, BYTE *pData, BYTE dataLength, BYTE txOptions, VOID_CALLBACKFUNC(completedFunc ) (auto  BYTE txStatus) )
{
    int i;
     byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
    idx = 0;
    set_node_id_in_buffer(nodeID);
    buffer[ idx++ ] = dataLength;

    for ( i = 0;i < dataLength;i++ )
        buffer[ idx++ ] = pData[ i ];

    buffer[ idx++ ] = txOptions;
    buffer[ idx++ ] = byCompletedFunc;	// Func id for CompletedFunc
    byLen = 0;
    cbFuncZWReplicationSendData = completedFunc;
    SendFrameWithResponse(FUNC_ID_ZW_REPLICATION_SEND_DATA, buffer, idx, buffer, &byLen );

    return buffer[ IDX_DATA ];
}

/**
  * \ingroup ZWCMD
  * Get capabilities of a controller.
  * \return
  *   = CONTROLLER_IS_SECONDARY :
  *      If bit is set then the controller is a secondary controller \n
  *   = CONTROLLER_ON_OTHER_NETWORK :
  *      If this bit is set then this controller is not using its build-in home ID \n
  *   = CONTROLLER_IS_SUC :
  *      If this bit is set then this controller is a SUC \n
  *   = CONTROLLER_NODEID_SERVER_PRESENT :
  *      If this bit is set then there is a SUC ID server (SIS)
  *      in the network and this controller can therefore
  *      include/exclude nodes in the network (inclusion controller). \n
  */
BYTE ZW_GetControllerCapabilities( void )
{
    idx = 0;
    byLen = 0;
    SendFrameWithResponse(FUNC_ID_ZW_GET_CONTROLLER_CAPABILITIES, buffer, idx, buffer, &byLen );

    return buffer[ IDX_DATA ];
}

/**
  \ingroup BASIS
  \macro{ ZW_REQUEST_NETWORK_UPDATE(FUNC) }
  Used to request network topology updates from the SUC/SIS node. The update is done on protocol level
  and any changes are notified to the application by calling the ApplicationControllerUpdate).
  Secondary controllers can only use this call when a SUC is present in the network. All controllers can
  use this call in case a SUC ID Server (SIS) is available.
  Routing Slaves can only use this call, when a SUC is present in the network. In case the Routing Slave
  has called ZW_RequestNewRouteDestinations prior to ZW_RequestNetWorkUpdate, then Return
  Routes for the destinations specified by the application in ZW_RequestNewRouteDestinations will be
  updated along with the SUC Return Route.
  \note
  The SUC can only handle one network update at a time, so care should be taken not to have all
  the controllers in the network ask for updates at the same time.
  \warning
  This API call will generate a lot of network activity that will use bandwidth and stress the
  SUC in the network. Therefore, network updates should be requested as seldom as possible and never
  more often that once every hour from a controller.

@return TRUE If the updating process is started.
        FALSE If the requesting controller is the SUC node or the SUC node is unknown.
@param completedFunc Transmit complete call back.
@param txStatus

ZW_SUC_UPDATE_DONE The update process succeeded.
ZW_SUC_UPDATE_ABORT The update process aborted because of
                   an error.
ZW_SUC_UPDATE_WAIT The SUC node is busy.
ZW_SUC_UPDATE_DISABLED The SUC functionality is disabled.
ZW_SUC_UPDATE_OVERFLOW The controller requested an update after more than 64 changes have occurred in
                   the network. The update information is then out of date in respect to that
                    controller. In this situation the controller have to make a replication
                    before trying to request any new network updates.
<b>Timeout:</b> 35s

<b>Exption recovery:</b> Resume normal operation, no recovery needed

 \serialapi{
 HOST->ZW: REQ | 0x53 | funcID
 ZW->HOST: RES | 0x53 | retVal
 ZW->HOST: REQ | 0x53 | funcID | txStatus
 }
 */
BOOL ZW_RequestNetWorkUpdate( void ( *complFunc ) ( BYTE ) )
{
    idx = 0;
    byCompletedFunc = ( complFunc == NULL ? 0 : 0x03 );
    buffer[ idx++ ] = byCompletedFunc;
    cbFuncZWRequestNetworkUpdate = complFunc;
    byLen = 0;
    SendFrameWithResponse(FUNC_ID_ZW_REQUEST_NETWORK_UPDATE, buffer, idx , buffer, &byLen );
    return buffer[ IDX_DATA ];
}

/**
 * \ingroup BASIS
 * \macro{ZW_EXPLORE_REQUEST_INCLUSION()}
 * This function sends out an explorer frame requesting inclusion into a network. If the inclusion request is
 * accepted by a controller in network wide inclusion mode then the application on this node will get notified
 * through the callback from the ZW_SetLearnMode() function. Once a callback is received from
 * ZW_SetLearnMode() saying that the inclusion process has started the application should not make
 * further calls to this function.
 * @return
 *  TRUE Inclusion request queued for transmission
 * @return
 *  FALSE Node is not in learn mode
 *
 *  \note Recommend not to call this function more than once every 4 seconds.
 *  \serialapi{
 *  HOST->ZW: REQ|0x5E
 *  ZW->HOST: RES|0x5E|retVal
 *  }
 */
BYTE ZW_ExploreRequestInclusion()
{
    idx = 0;
    byLen = 0;
    SendFrameWithResponse(FUNC_ID_ZW_EXPLORE_REQUEST_INCLUSION, buffer, idx , buffer, &byLen );
    return buffer[ IDX_DATA ];
}

BYTE ZW_ExploreRequestExclusion()
{
    idx = 0;
    byLen = 0;
    SendFrameWithResponse(FUNC_ID_ZW_EXPLORE_REQUEST_EXCLUSION, buffer, idx , buffer, &byLen );
    return buffer[ IDX_DATA ];
}

/**
 * \ingroup BASIS
 * \macro{ZW_GET_PROTOCOL_STATUS()}
 * Report the status of the protocol.
 * The function return a mask telling which protocol function is currently running
 * @return
 * Zero Protocol is idle.
 * @return
 * ZW_PROTOCOL_STATUS_ROUTING Protocol is analyzing the routing table.
 * @return
 * ZW_PROTOCOL_STATUS_SUC  SUC sends pending updates.
 * \serialapi{
 * HOST->ZW: REQ | 0xBF
 * ZW->HOST: RES | 0xBF | retVal
 * }
 */
BYTE ZW_GetProtocolStatus() {
  idx = 0;
  byLen = 0;
  SendFrameWithResponse(FUNC_ID_ZW_GET_PROTOCOL_STATUS, buffer, idx , buffer, &byLen );
  return buffer[ IDX_DATA ];
}

/**
  * \ingroup ZWCMD
  * Get the Z Wave library type.
  * \return
  *   = ZW_LIB_CONTROLLER_STATIC	Static controller library
  *   = ZW_LIB_CONTROLLER_BRIDGE	Bridge controller library
  *   = ZW_LIB_CONTROLLER	        Portable controller library
  *   = ZW_LIB_SLAVE_ENHANCED	    Enhanced slave library
  *   = ZW_LIB_SLAVE_ROUTING	    Routing slave library
  *   = ZW_LIB_SLAVE	            Slave library
  *   = ZW_LIB_INSTALLER	        Installer library
  */
BYTE ZW_Type_Library( void )
{
    idx = 0;
    byLen = 0;
    idx++;
    SendFrameWithResponse(FUNC_ID_ZW_TYPE_LIBRARY, 0, 0 , buffer, &byLen );

    return buffer[ IDX_DATA ];
}

/**
  * \ingroup ZWCMD
  * Lock a response route for a specific node.
  * \param[in] bNodeID
  */
void ZW_ReplicationReceiveComplete()
{
    SendFrame(FUNC_ID_ZW_REPLICATION_COMMAND_COMPLETE, 0, 0 );
}

/**
  * \ingroup ZWCMD
  * Make the Z-Wave chip reset itself.
  * Recent Z-Wave versions (at least 6.80 and 7.00) will return FUNC_ID_SERIAL_API_STARTED once restarted.
  */
void ZW_SoftReset()
{
   SendFrame(FUNC_ID_SERIAL_API_SOFT_RESET, buffer, idx);
   // Node id base type falls back to default 8 bit on soft reset
   if (lr_enabled && (ZW_RFRegionGet() == RF_US_LR)) {
     WRN_PRINTF("SoftReset called while Long Range enabled, Setting the Node id base type to 16 bit again\n");
     if (SerialAPI_EnableLR() == false) {
       LOG_PRINTF("Failed to enable Z-Wave Long Range capability\n");
     }
   }
}

/**
 * Get the current power level used in RF transmitting.
 * \note
 * This function should only be used in an install/test link situation.
 * \return
 * The power level currently in effect during RF transmissions
 * \sa ZW_RFPowerLevelSet
 *
 * \serialapi{
 * HOST->ZW: REQ | 0xBA
 * ZW->HOST: RES | 0xBA | powerlevel
 * }

 */
BYTE                /*RET The current powerlevel */
ZW_RFPowerLevelGet(
  void)            /* IN Nothing */
{
  idx = 0;
  byLen = 0;
  SendFrameWithResponse(FUNC_ID_ZW_RF_POWER_LEVEL_GET,buffer, idx , buffer, &byLen );
  return buffer[ IDX_DATA ];
}

/**
 * Get the current TX powerlevel
 * \return
 * The TX powerlevel currently in effect
 * \sa ZW_TXPowerLevelGet
 *
 * \serialapi{
 * HOST->ZW: REQ | 0x0B | 0x08
 * ZW->HOST: RES | 0x0B | 0x08 | NormalTxPower | Measure0dBmPower
 * }
 */
TX_POWER_LEVEL
ZW_TXPowerLevelGet(
  void)
{
  TX_POWER_LEVEL txpowerlevel;
  idx = 0;
  byLen = 0;
  buffer[idx++] = SERIAL_API_SETUP_CMD_TX_POWERLEVEL_GET;
  SendFrameWithResponse(FUNC_ID_SERIALAPI_SETUP, buffer, idx, buffer, &byLen);
  txpowerlevel.normal = buffer[IDX_DATA + 1];
  txpowerlevel.measured0dBm = buffer[IDX_DATA + 2];
  return txpowerlevel;
}

/**
 * Get the current MAX LR TX powerlevel
 * \return
 * The TX powerlevel currently in effect
 * \sa ZW_TXPowerLevelGet
 *
 * \serialapi{
 * HOST->ZW: REQ | 0x0B | 0x05
 * ZW->HOST: RES | 0x0B | 0x05 | MAX LR TxPower MSB | MAX LR TxPower LSB
 * }
 */
int16_t
ZW_MAXLRTXPowerLevelGet(
  void)
{
  int16_t max_lr_txpowerlevel;
  idx = 0;
  byLen = 0;
  buffer[idx++] = SERIAL_API_SETUP_CMD_MAX_LR_TX_PWR_GET;
  SendFrameWithResponse(FUNC_ID_SERIALAPI_SETUP, buffer, idx, buffer, &byLen);
  max_lr_txpowerlevel = buffer[IDX_DATA + 1] << 8;
  max_lr_txpowerlevel |= buffer[IDX_DATA + 2];
  return max_lr_txpowerlevel;
}


/**
 * Get the current RF region
 * \return
 * The RF region currently in effect
 * \sa ZW_RFRegionGet
 *
 * \serialapi{
 * HOST->ZW: REQ | 0x0B | 0x20
 * ZW->HOST: RES | 0x0B | 0x20 | RFRegion
 * }

 */
BYTE
ZW_RFRegionGet(
  void)
{
  idx = 0;
  byLen = 0;
  buffer[idx++] = SERIAL_API_SETUP_CMD_RF_REGION_GET;
  SendFrameWithResponse(FUNC_ID_SERIALAPI_SETUP, buffer ,idx, buffer, &byLen);
  return buffer[IDX_DATA + 1];
}

/* See Serialpi.h for doxygen documentation */
BYTE SerialAPI_ZW_SendDataMulti_Bridge(uint16_t srcNodeID,
                                       nodemask_t dstNodeMask,
                                       BYTE *data,
                                       BYTE dataLength,
                                       BYTE txOptions,
                                       VOID_CALLBACKFUNC(completedFunc)(BYTE txStatus))
{
  BYTE numberOfNodes = 0;
  BYTE numberOfNodesIdx = 0;
  BYTE funcId = 0;
  BYTE responseLen = 0;
  int idx = 0;

  set_node_id_in_buffer(srcNodeID);

  /*
   * The number of nodes should precede the list of nodes in the data. But
   * we must first unpack the nodemask to learn how many nodes it contain.
   * We'll save the position for now (and increment idx) and fill in the
   * count when we have processed the nodemask.
   */
  numberOfNodesIdx = idx++;

  /* Unpack nodemask in byte array into array of node ids */
  for (int node_id = 1; node_id <= ZW_MAX_NODES; node_id++)
  {
    if (nodemask_test_node(node_id, dstNodeMask))
    {
      buffer[idx++] = node_id;
      numberOfNodes++;
    }
  }

  buffer[numberOfNodesIdx] = numberOfNodes;
  buffer[idx++] = dataLength;

  for (int i = 0; i < dataLength; i++ )
  {
    buffer[idx++] = data[i];
  }

  buffer[idx++] = txOptions;

  /* Register the function to call if/when a SendDataMulti_Bridge
   * callback request is received via the Serial API.
   */
  cbFuncZWSendDataMultiBridge = completedFunc;
  if (completedFunc)
  {
    /* If a callback function has been specified the funcId sent via
     * the serial API must be set to a non-zero value to tell the
     * serial API that we want a callback when the transmission
     * has completed. (0x03 is just a dummy non-zero value. It's
     * not inspected by the gateway callback handler)
     */
    funcId = 0x03;
  }

  buffer[idx++] = funcId;

  /* Note that the response will be placed into the same buffer
   * where data to send is located */
  SendFrameWithResponse(FUNC_ID_ZW_SEND_DATA_MULTI_BRIDGE, buffer, idx, buffer, &responseLen);

  /* The response data contains a BOOL return value from the Z-Wave chip */
  return (responseLen > IDX_DATA) ? buffer[ IDX_DATA ] : FALSE;
}


void SerialAPI_WatchdogStart()
{
    /*  HOST -> ZW: REQ | 0x1C */
    SendFrame(FUNC_ID_ZW_WATCHDOG_START, 0, 0);
}

/**
 * The Serial API function 0x1C makes use of the ZW_GetRandomWord to generate a specified number
 * of random bytes and takes care of the handling of the RF:
 *  - Set the RF in powerdown prior to calling the ZW_GetRandomWord the first time, if not
 *    possible then return result to HOST.
 *  - Call ZW_GetRandomWord until enough random bytes generated or ZW_GetRandomWord
 *    returns FALSE.
 *  - Call ZW_GetRandomWord with bResetRadio = TRUE to reinitialize the radio.
 *  - Call ZW_SetRFReceiveMode with TRUE if the serialAPI hardware is a listening device or with
 *    FALSE if it is a non-listening device.
 *  - Return result to HOST.
 *
 *  @param count        Number of random bytes needed. Returned Range 1...32 random bytes are supported.
 *  @param randomBytes  Destination buffer.
 *
 * serialapi{
 * HOST -> ZW: REQ | 0x1C | noRandomBytes
 * ZW -> HOST: RES | 0x1C | randomGenerationSuccess | noRandomBytesGenerated | randombytes[]
 * }
 */
BOOL SerialAPI_GetRandom(BYTE count, BYTE* randomBytes) {
  idx=0;
  buffer[ idx++ ] = count;
  byLen = 0;

  ASSERT(count <= 32);

  SendFrameWithResponse(FUNC_ID_ZW_GET_RANDOM, buffer, idx, buffer, &byLen );

  //Check randomGenerationSuccess and noRandomBytesGenerated
  if((!buffer[IDX_DATA]) || (buffer[IDX_DATA+1] < count)) {
      ASSERT(0);
      return FALSE; 
  }

  memcpy(randomBytes,&buffer[IDX_DATA + 2],buffer[ IDX_DATA +1]);
  return TRUE;
}

void ZW_GetBasic( BYTE *pData)
{
    int i;
    idx = 0;
    /*  bLine | bRemoveBad | bRemoveNonReps | funcID */
    byLen = 0;
    SendFrameWithResponse(BASIC_GET, 0, 0 , buffer, &byLen );

    for ( i = 0;i < 29;i++ )
    {
        pData[ i ] = buffer[ IDX_DATA + i +3];
    }
}

/**
 * This function is used to request whether the controller is a primary controller or a secondary controller in
 * the network.
 * Defined in: ZW_controller_api.h
 * \return TRUE when the controller is a
 * primary controller in the network. FALSE when the controller is a
 * secondary controller in the network.
 */
BOOL ZW_IsPrimaryCtrl (void) {
  return ((ZW_GetControllerCapabilities() & CONTROLLER_IS_SECONDARY) == 0);
}

#ifdef SECURITY_SUPPORT
#include "rijndael-alg-fst.h"
BOOL SerialAPI_AES128_Encrypt(const BYTE *ext_input, BYTE *ext_output, const BYTE *cipherKey) CC_REENTRANT_ARG{
  int Nr; /* key-length-dependent number of rounds */
  u32 rk[4*(MAXNR + 1)]; /* key schedule */
  /*if(SupportsCommand(FUNC_ID_ZW_AES_ECB)) {
    memcpy(&buffer[0],cipherKey,16);
    memcpy(&buffer[16],ext_input,16);
    SendFrameWithResponse(FUNC_ID_ZW_AES_ECB, buffer, 32 , buffer, &byLen );
    memcpy(ext_output,&buffer[IDX_DATA],16);
    return 1;
  } else*/ {
    Nr = rijndaelKeySetupEnc(rk, cipherKey, 128);
    rijndaelEncrypt(rk, Nr, ext_input, ext_output);
    return 1;
  }
}
#endif

void ZW_WatchDogDisable() {
  SendFrame(FUNC_ID_ZW_WATCHDOG_DISABLE,0, 0);
}

void ZW_WatchDogEnable() {
  SendFrame(FUNC_ID_ZW_WATCHDOG_ENABLE,0, 0);
}

BYTE ZW_isFailedNode(uint16_t nodeID) {
  idx = 0;
  byLen = 0;
  set_node_id_in_buffer(nodeID);
  SendFrameWithResponse(FUNC_ID_ZW_IS_FAILED_NODE_ID, buffer, idx , buffer, &byLen );
  return buffer[IDX_DATA];
}

/**
 * Use this function to set the maximum number of source routing attempts before the explorer frame mechanism kicks
 * in. Default value with respect to maximum number of source routing attempts is five.
 * Remember to enable the explorer frame mechanism by setting the transmit option flag
 * TRANSMIT_OP TION_EXPLORE in the send data calls.
 * A ZDK 4.5 controller uses the routing algorithm from 5.02 to address nodes from
 * ZDK's not supporting explorer frame.
 * The routing algorithm from 5.02 ignores the transmit option *TRANSMIT_OPTION_EXPLORE flag and maximum number of source routing attempts value
 * (maxRouteTries)
 */
void ZW_SetRoutingMAX(BYTE maxRouteTries) {
  idx = 0;
  byLen = 0;
  buffer[idx++] = maxRouteTries;
  SendFrameWithResponse(FUNC_ID_ZW_SET_ROUTING_MAX, buffer, idx , buffer, &byLen );
  return;
}


/* Enable Auto Programming over serial interface */
void ZW_AutoProgrammingEnable(void) {
  /* Chip will reboot after receiving this command,
   * so send as raw bytes without waiting for SerialAPI ACKs */
  uint8_t buf[5] = { 0x01, 0x03, 0x00, 0x27, 0xdb};
  SerialPutBuffer(buf, 5);
}



/**
 *  Use this API call to get the Last Working Route (LWR) for a destination node if any exist.
 *  The LWR is the
 *  last successful route used between sender and destination node. The LWR is stored in NVM.
 * @return TRUE A LWR exists for bNodeID and the found route placed in the
 *      5-byte array pointed out by pLastWorkingRoute.
 *         FALSE No LWR found for bNodeID.
 * @param[in] bNodeID IN The Node ID (1...232) specifies the destination node whom the LWR is wanted from.
 * @param[in] pLastWorkingRoute Pointer to a 5-byte array where the wanted LWR will be written.
 The 5-byte array contains in the first 4 byte the max 4 repeaters (index 0 -3) and
 1 routespeed byte (index 4) used in the LWR. The LWR which pLastWorkingRoute
 points to is valid if function return value equals
 TRUE. The first repeater byte (starting from index 0) equaling zero indicates no more
 repeaters in route. If the repeater at index 0 is zero then the LWR is direct. The routespeed
 byte (index 4) can be either ZW_LAST_WORKING_ROUTE_SPEED_9600,ZW_LAST_WORKING_ROUTE_SPEED_40K
 or ZW_LAST_WORKING_ROUTE_SPEED_100K

 Serial API
  HOST->ZW: REQ | 0x92 | bNodeID
  ZW->HOST: RES | 0x92 | bNodeID | retVal | repeater0 | repeater1 | repeater2 | repeater3 | routespeed
 */
BYTE ZW_GetPriorityRoute(BYTE bNodeID, XBYTE *pLastWorkingRoute) {
  idx = 0;
  byLen = 0;

  buffer[idx++] = bNodeID;
  SendFrameWithResponse(FUNC_ID_ZW_GET_LAST_WORKING_ROUTE, buffer, idx , buffer, &byLen );
  ASSERT(bNodeID == buffer[IDX_DATA]);
  memcpy(pLastWorkingRoute,&buffer[IDX_DATA + 2],5);
  return buffer[IDX_DATA+1];
}


/**
 * Use this API call to set the Last Working Route (LWR) for a destination node. The LWR is the last
 * successful route used between sender and destination node. The LWR is stored in NVM.
 *
 * @return BOOL TRUE The LWR for bNodeID was successfully set to the specified5-byte
 * LWR pointed out by pLastWorkingRoute. FALSE The specified bNodeID was not valid and no LWR was set.
 *
 * @param[IN] bNodeID The Node ID (1...232) - specifies the destination node for whom the LWR is to be set.
 * @param[IN] pLastWorkingRoute Pointer for a 5-byte array containing the new LWR to be set.
 * The 5-byte array contains 4 repeater node bytes (index 0 - 3) and 1 routespeed byte (index 4).
 * The first repeater byte (starting from index 0) equaling zero indicates no more repeaters in route.
 * If the repeater at index 0 is zero then the LWR is direct. The routespeed byte (index 4) can be either
 * ZW_LAST_WORKING_ROUTE_SPEED_9600,
 * ZW_LAST_WORKING_ROUTE_SPEED_40K
 * or
 * ZW_LAST_WORKING_ROUTE_SPEED_100K
 * Serial API
 * HOST->ZW: REQ | 0x93 | bNodeID | repeater0 | repeater1 | repeater2 | repeater3 | routespeed
 * ZW->HOST: RES | 0x93 | bNodeID | retVa
 */
BOOL ZW_SetPriorityRoute(BYTE bNodeID, XBYTE *pLastWorkingRoute) {

  idx=0;
  buffer[idx++] = bNodeID;
  buffer[idx++] = pLastWorkingRoute[0];
  buffer[idx++] = pLastWorkingRoute[1];
  buffer[idx++] = pLastWorkingRoute[2];
  buffer[idx++] = pLastWorkingRoute[3];
  buffer[idx++] = pLastWorkingRoute[4];
  SendFrameWithResponse(FUNC_ID_ZW_SET_LAST_WORKING_ROUTE, buffer, idx , buffer, &byLen );
  return buffer[IDX_DATA];
}

/*========================   ZW_AddNodeToNetworkSmartStart   ======================
**
**    Add any type of node to the network and accept prekit inclusion.
**    This should be called after receiving a ApplicationControllerUpdate
**    with a DSK which is present in provisioning list.
**
**    The modes is always ADD_NODE_HOMEID 0x07,
**
**    potentially coupled with these flags
**
**    ADD_NODE_OPTION_HIGH_POWER    Set this flag in bMode for High Power inclusion.
**    ADD_NODE_OPTION_NETWORK_WIDE  Set this flag in bMode for Network wide inclusion
**
**    DSK is a pointer to the 16-byte DSK.
**
**    Side effects:
**
**--------------------------------------------------------------------------*/
void
ZW_AddNodeToNetworkSmartStart(BYTE bMode, BYTE *dsk,
                    VOID_CALLBACKFUNC(completedFunc)(auto LEARN_INFO*))
{
    idx = 0;
    byCompletedFunc = ( completedFunc == NULL ? 0 : 0x03 );
    buffer[ idx++ ] = bMode;
    buffer[ idx++ ] = byCompletedFunc;
    buffer[ idx++ ] = dsk[0];
    buffer[ idx++ ] = dsk[1];
    buffer[ idx++ ] = dsk[2];
    buffer[ idx++ ] = dsk[3];
    buffer[ idx++ ] = dsk[4];
    buffer[ idx++ ] = dsk[5];
    buffer[ idx++ ] = dsk[6];
    buffer[ idx++ ] = dsk[7];
    cbFuncAddNodeToNetwork = completedFunc;
    SendFrame(FUNC_ID_ZW_ADD_NODE_TO_NETWORK, buffer, idx );
}

/*========================   ZW_GetBackgroundRSSI   ======================
 *
 * Returns the latest  value of the background RSSI
 *
 * Returns an array of RSSI values
 * and a length of that array.
 *
 * values must be an array with a length of 3 or more bytes.
 *
 * HOST->ZW: (no arguments)
 * ZW->HOST: RES | RSSI Ch0 | RSSI Ch1 | RSSI Ch2 (3CH systems only)
 *
 *--------------------------------------------------------------------------*/
void
ZW_GetBackgroundRSSI(BYTE *rssi_values, BYTE *values_length)
{
  BYTE replyValues[10];
  BYTE numChannels; /* Number of channels returned*/
  SendFrameWithResponse(FUNC_ID_ZW_GET_BACKGROUND_RSSI, buffer, 0, replyValues, &numChannels);
  numChannels = numChannels - IDX_DATA; /* Total reply lenght is IDX_DATA more than number of channels */
  if(numChannels > 4) {
    numChannels = 4;
    WRN_PRINTF("ZW_GetBackgroundRSSI: Too much data returned\n");
  }
  *values_length = numChannels;
  memcpy(rssi_values, &replyValues[IDX_DATA], numChannels);
}


void 
ZW_NVRGetValue(BYTE offset, BYTE bLength, BYTE* pNVRValue)
{
  BYTE reply_len;
  buffer[0] = offset;
  buffer[1] = bLength;
  SendFrameWithResponse(FUNC_ID_NVR_GET_VALUE, buffer, 2, buffer, &reply_len);
  for(int i=0; i < bLength; i++ ) {
    pNVRValue[i] = buffer[IDX_DATA+i];
  }
}
