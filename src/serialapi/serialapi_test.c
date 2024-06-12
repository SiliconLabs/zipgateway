/* © 2014 Silicon Laboratories Inc.
 */
#include "Serialapi.h"
#include <ZW_controller_api.h>
#include <stdio.h>
#ifndef _MSC_VER
/* According to POSIX.1-2001 */
#include <sys/select.h>
/* According to earlier standards */
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#else
#undef BOOL
#undef BYTE
#undef PBYTE
#undef code
#include<windows.h>
#include<winbase.h>
#endif



/* A list of the known command classes. Except the basic class which allways */
/* should be supported. Used when node info is send */
const APPL_NODE_TYPE MyType = {GENERIC_TYPE_ZIP_GATEWAY,SPECIFIC_TYPE_ZIP_ADV_GATEWAY};
const BYTE MyClasses[] = { COMMAND_CLASS_NO_OPERATION, COMMAND_CLASS_SIMPLE_AV_CONTROL };

const BYTE NumberOfClasses = sizeof(MyClasses);

#define COMMAND_CLASS_SIMPLE_AV_CONTROL                                                  0x94

/* Simple Av Control command class commands */
#define SIMPLE_AV_CONTROL_VERSION                                                        0x01
#define SIMPLE_AV_CONTROL_GET                                                            0x02
#define SIMPLE_AV_CONTROL_REPORT                                                         0x03
#define SIMPLE_AV_CONTROL_SET                                                            0x01
#define SIMPLE_AV_CONTROL_SUPPORTED_GET                                                  0x04
#define SIMPLE_AV_CONTROL_SUPPORTED_REPORT                                               0x05
/* Values used for Simple Av Control Set command */
#define SIMPLE_AV_CONTROL_SET_PROPERTIES1_KEY_ATTRIBUTES_MASK                            0x07
#define SIMPLE_AV_CONTROL_SET_PROPERTIES1_RESERVED_MASK                                  0xF8
#define SIMPLE_AV_CONTROL_SET_PROPERTIES1_RESERVED_SHIFT                                 0x03

#define SIMPLE_AV_CMD_PLAY    0x0013
#define SIMPLE_AV_CMD_STOP    0x0014
#define SIMPLE_AV_CMD_CH_UP     0x0004
#define SIMPLE_AV_CMD_CH_DOWN   0x0005
#define SIMPLE_AV_CMD_VOL_UP    0x0003
#define SIMPLE_AV_CMD_VOL_DOWN  0x0002
#define SIMPLE_AV_CMD_MUTE    0x0001
#define SIMPLE_AV_CMD_MENU    0x001D
#define SIMPLE_AV_CMD_MENU_UP      0x001E
#define SIMPLE_AV_CMD_MENU_DOWN    0x001F
#define SIMPLE_AV_CMD_MENU_LEFT    0x0020
#define SIMPLE_AV_CMD_MENU_RIGHT   0x0021
#define SIMPLE_AV_CMD_SELECT  0x0024
#define SIMPLE_AV_CMD_PAGEUP  0x0022
#define SIMPLE_AV_CMD_PAGEDOWN  0x0023
#define SIMPLE_AV_CMD_POWER    0x0027

typedef struct _AV_CMD_STRINGS_
{
  unsigned short avCmd;
  unsigned char* str;
}AV_CMD_STRINGS;

AV_CMD_STRINGS avCmdStrings[] = {{SIMPLE_AV_CMD_PLAY,"Key: PLAY"},{SIMPLE_AV_CMD_STOP,"Key: STOP"},
                                 {SIMPLE_AV_CMD_CH_UP,"Key: CHANNEL UP"},{SIMPLE_AV_CMD_CH_DOWN,"Key: CHANNEL DOWN"},
                                 {SIMPLE_AV_CMD_VOL_UP,"Key: VOLUME UP"},{SIMPLE_AV_CMD_VOL_DOWN,"Key: VOLUME DOWN"},
                                 {SIMPLE_AV_CMD_MUTE,"Key: MUTE"},{SIMPLE_AV_CMD_MENU,"Key: MENU"},
                                 {SIMPLE_AV_CMD_MENU_UP,"Key: MENU UP"},{SIMPLE_AV_CMD_MENU_DOWN,"Key: MENU DOWN"},
                                 {SIMPLE_AV_CMD_MENU_LEFT,"Key: MENU LEFT"},{SIMPLE_AV_CMD_MENU_RIGHT,"Key: MENU RIGHT"},
                                 {SIMPLE_AV_CMD_SELECT,"Key: SELECT"},{SIMPLE_AV_CMD_PAGEUP,"Key: PAGE UP"},
                                 {SIMPLE_AV_CMD_PAGEDOWN,"Key: PAGE DOWN"},{SIMPLE_AV_CMD_POWER,"Key: POWER"},
                                 {0,"NOT DEFINED"}
                                };


void
ApplicationCommandHandler(
  BYTE  rxStatus,                   /*IN  Frame header info */
#if defined(ZW_CONTROLLER) && !defined(ZW_CONTROLLER_STATIC) && !defined(ZW_CONTROLLER_BRIDGE)
  BYTE  destNode,                  /*IN  Frame destination ID, only valid when frame is not Multicast*/
#endif
  BYTE  sourceNode,                 /*IN  Command sender Node ID */
  ZW_APPLICATION_TX_BUFFER *pCmd,  /*IN  Payload from the received frame, the union
                                          should be used to access the fields*/
  BYTE   cmdLength) CC_REENTRANT_ARG                /*IN  Number of command bytes including the command */
{
     printf("ApplicationCommandHandler node %d class %d size %d\n",sourceNode,pCmd->ZW_Common.cmdClass,cmdLength);
     if(pCmd->ZW_Common.cmdClass == COMMAND_CLASS_SIMPLE_AV_CONTROL) {
	switch (pCmd->ZW_Common.cmd)
	{
	  case SIMPLE_AV_CONTROL_SET:
{
	    unsigned short index;
	    unsigned short avCommand = pCmd->ZW_SimpleAvControlSet1byteFrame.variantgroup1.command1;
	    avCommand <<=8;
	    avCommand |= pCmd->ZW_SimpleAvControlSet1byteFrame.variantgroup1.command2;
	    for (index = 0; avCmdStrings[index].avCmd != 0;index++)
	    {
	      if (avCommand == avCmdStrings[index].avCmd)
	      {
	        break;
	      }
	    }
	    printf("SIMPLE_AV_CONTROL_SET \n");
      printf("%s ",avCmdStrings[index].str);
	    if ((pCmd->ZW_SimpleAvControlSet1byteFrame.properties1 & 0x07) == 0)
	    {
	      printf("Attribute :Key Down\n");
	    }
	    else  if ((pCmd->ZW_SimpleAvControlSet1byteFrame.properties1 & 0x07) == 1)
	    {
	      printf("Attribute :Key Up\n");
	    }
	    else  if ((pCmd->ZW_SimpleAvControlSet1byteFrame.properties1 & 0x07) == 2)
	    {
	      printf("Attribute :Keep Alive\n");
	    }
	    else
	    {
	      printf("Attribute :UNKNOWN \n");
	    }

}
	  break;
	}
  }
}

void ApplicationNodeInformation(BYTE *deviceOptionsMask, /*OUT Bitmask with application options    */
		APPL_NODE_TYPE *nodeType, /*OUT  Device type Generic and Specific   */
		BYTE **nodeParm, /*OUT  Device parameter buffer pointer    */
		BYTE *parmLength /*OUT  Number of Device parameter bytes   */
		) {
	/* this is a listening node and it supports optional CommandClasses */
	*deviceOptionsMask = APPLICATION_NODEINFO_LISTENING
		| APPLICATION_NODEINFO_OPTIONAL_FUNCTIONALITY;
	nodeType->generic = MyType.generic; /* Generic device type */
	nodeType->specific = MyType.specific; /* Specific class */
	*nodeParm = (BYTE*)MyClasses; /* Send list of known command classes. */
	*parmLength = sizeof(MyClasses); /* Set length*/
}

void ApplicationControllerUpdate(BYTE bStatus, /*IN  Status event */
		BYTE bNodeID, /*IN  Node id of the node that send node info */
		BYTE* pCmd, /*IN  Pointer to Application Node information */
		BYTE bLen /*IN  Node info length                        */
		) {
	printf("Got node info from node %d\n",bNodeID);
}

/*
 * Setup callback funtions. See Z-Wave Application Programmer
 * guide for information on what their roles are.
 */
const struct SerialAPI_Callbacks serial_api_callbacks = {
	ApplicationCommandHandler,
	ApplicationNodeInformation,
	ApplicationControllerUpdate,
	0,
	0,
	0,
	0,
};

#ifdef _MSC_VER
int chready() {
  //
  return (WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE), 2) & WAIT_TIMEOUT)==0; 
}

#else
int chready() {
	fd_set rfds;
	struct timeval tv;
	int retval;

	/* Watch stdin (fd 0) to see when it has input. */
	FD_ZERO(&rfds);
	FD_SET(0, &rfds);

	/* Wait up to five seconds. */
	tv.tv_sec = 0;
	tv.tv_usec = 100*1000;

	retval = select(1, &rfds, NULL, NULL, &tv);
	/* Don't rely on the value of tv now! */

	if (retval == -1)
		return 0;
	else if (retval)
		return 1;
	else
		return 0;
}
#endif

static void AddNodeStatusUpdate(LEARN_INFO* inf) {
	printf("AddNodeStatusUpdate status=%d info len %d\n",inf->bStatus,inf->bLen);

	switch(inf->bStatus) {
		case ADD_NODE_STATUS_LEARN_READY:
			break;
		case ADD_NODE_STATUS_NODE_FOUND:
			break;
		case ADD_NODE_STATUS_ADDING_SLAVE:
		case ADD_NODE_STATUS_ADDING_CONTROLLER:
			if(inf->bLen) {
				printf("Node added with nodeid %d\n", inf->bSource );
			}
			break;
		case ADD_NODE_STATUS_PROTOCOL_DONE:
			ZW_AddNodeToNetwork(ADD_NODE_STOP,AddNodeStatusUpdate);
			break;
		case ADD_NODE_STATUS_DONE:
		case ADD_NODE_STATUS_FAILED:
		case ADD_NODE_STATUS_NOT_PRIMARY:
			break;
	}
}


void RemoveNodeStatusUpdate(LEARN_INFO* inf) {
  printf("RemoveNodeStatusUpdate status=%d\n",inf->bStatus);
  switch(inf->bStatus) {
  case ADD_NODE_STATUS_LEARN_READY:
    break;
  case REMOVE_NODE_STATUS_NODE_FOUND:
    printf("Removing node %d\n",inf->bSource);
    break;
  case REMOVE_NODE_STATUS_DONE:
    break;
  case REMOVE_NODE_STATUS_FAILED:
    break;
  }

}

void SetDefaultUpdate(){
    printf("Set Default done\n");
}



void test() {
	printf("Hello\n");
}

int main(int argc, char** argv) {
	char buf[64];
	int k;
        NODEINFO nif;
	memset(buf,0,sizeof(buf));
	if(argc!=2) {
		fprintf(stderr, "Usage: %s <serialport_dev>\n",argv[0]);
		return 1;
	}

	if(!SerialAPI_Init(argv[1],&serial_api_callbacks)) {
	  fprintf(stderr,"SerialAPI not initilized\n");
	  return 1;
	}
	ZW_Version(buf);
	printf("Vesion: %s\n", buf );
	ZW_TIMER_START(test,100,TIMER_FOREVER);

	
	ZW_SoftReset();
	while(1) {
		SerialAPI_Poll();

		/*		if(ZW_Version(buf) ) {
				printf("Vesion: %s\n", buf );
				} else {
				printf("Error\n");
				}*/
		if(chready()) {
			switch(getc(stdin)) {
				case 'a':
					printf("Addnode start\n");
					ZW_AddNodeToNetwork(ADD_NODE_ANY|ADD_NODE_OPTION_NETWORK_WIDE,AddNodeStatusUpdate);
					break;
				case 'r':
					printf("Remove node start\n");
					ZW_RemoveNodeFromNetwork(REMOVE_NODE_ANY,RemoveNodeStatusUpdate);
					break;
				case 'd':
					printf("Set Default start\n");
					ZW_SetDefault(SetDefaultUpdate);
					break;
				case 'q':
					printf("Quit\n");
					return 0;
			}
		}
	}
	return 0;
}
