/* © 2014 Silicon Laboratories Inc.
 */

#ifndef ZIP_ROUTER_CONFIG_H_
#define ZIP_ROUTER_CONFIG_H_

/* Define here to avoid including serialapi.h */
#define ZW_CONTROLLER
#define ZW_CONTROLLER_BRIDGE

#include "TYPES.H"  /* XBYTE for ZW_basis_api.h */
#include "ZW_basis_api.h"  /* TX_POWER_LEVEL */
#include "ZW_classcmd_ex.h" /* mailbox_configuration_mode_t */
#include "zw_appl_nvm.h"

/* No contiki ipv6 addr available */
#if !UIP_CONF_IPV6
typedef union uip_ip6addr_t {
  u8_t  u8[16];     /* Initializer, must come first!!! */
  u16_t u16[8];
} uip_ip6addr_t ;
#else
/* Use Contiki uip_ip6addr_t */
#include "net/uip.h"
#endif

/**
 * This structure holds configurable but runtime static
 * variable used to setup the Z/IP router network.
 */
struct router_config {

  /**
   * Name of serial port to use. This string is parsed to \ref SerialInit upon intialization.
   */
  const char* serial_port;

  /**
   * Destination of unsolicitated ZW frames.
   * Configuration parameter ZipUnsolicitedDestinationIp6 in zipgateway.cfg.
   */
  uip_ip6addr_t unsolicited_dest;
  /**
   * Destination port for unsolicitated ZW frames.
   * Configuration parameter ZipUnsolicitedDestinationPort in zipgateway.cfg.
   */
  u16_t unsolicited_port;

  /**
   * Second destination of unsolicitated ZW frames.
   * Configuration parameter ZipUnsolicitedDestination2Ip6 in zipgateway.cfg.
   */
  uip_ip6addr_t unsolicited_dest2;
  /**
   * Second destination port for unsolicitated ZW frames.
   * Configuration parameter ZipUnsolicitedDestination2Port in zipgateway.cfg.
   */
  u16_t unsolicited_port2;

/**
 * Prefix of Z-Wave HAN (we used to call it pan :-) )
 *
 * Used to generate ip addresses for devices on the PAN side.
 * Copied from cfg_pan_prefix if that is set.
 * Otherwise its copied from the nvm_config.ula_pan_prefix.
 */
  uip_ip6addr_t pan_prefix;

  /**
   * Configuration parameter TunScript in zipgateway.cfg.
   */
  uip_ip6addr_t tun_prefix;
  /**
   * Configuration parameter ZipTunIp6PrefixLength in zipgateway.cfg.
   */
  u8_t tun_prefix_length;

  /**
   * IPv6 address of Z/IP router LAN side.
   *
   * The active LAN address used by the gateway, its a copy of cfg_lan_addr or the nvm_config.ula_lan_addr
   */
  uip_ip6addr_t lan_addr;
  /**
   *
   * The active LAN prefix length.
   * Default 64.
   */
  u8_t lan_prefix_length;

  /**
   * Configuration parameter ZipLanIp6 in zipgateway.cfg.
   * This is the full ip address which tha gateway uses on the LAN. If this is set
   * to the zero address, the gateway will use a ULA address which is stored in the
   * nvm_config_t
   */
  uip_ip6addr_t cfg_lan_addr;   //Lan address from config file
  /**
   * Configuration parameter ZipPanIp6 in zipgateway.cfg.
   *
   * The IPv6 pan prefix, this the 64bit prefix for the PAN.
   * The pan address a created as pan_prefix::nodeID. If this the zero address the
   * gateway will use a the ula prefix stored in the nvm_config.
   */
  uip_ip6addr_t cfg_pan_prefix; //PAN address from config file

  /**
   * Configuration parameter ZipLanIp6PrefixLength in zipgateway.cfg.
   *
   * Must be 64.  Copied to lan_prefix_length before use.
   */
  u8_t cfg_lan_prefix_length;  //Not used on ZIP_Gateway..always 64

  /**
   * IPv6 default gateway/next hop on the LAN side.
   * Configuration parameter ZipLanGw6 in zipgateway.cfg.
   *
   * This must be set, if the gateway needs to communcate with IPv6 nodes
   * beyond the LAN prefix. If this is zero, the gateway will only be able
   * to communicate with nodes on LAN prefix.
   */
  uip_ip6addr_t gw_addr;

  /**
   * Path to script that controls the "Node Identify" indicator LED.
   *
   * Configuration parameter ZipNodeIdentifyScript in zipgateway.cfg.
   * See CallExternalBlinker() in CC_indicator.c
   */
  const char *node_identify_script;

  /** Configuration parameter SerialLog in zipgateway.cfg.
   *
   */
  const char* serial_log;
  /** Configuration parameter LogLevel in zipgateway.cfg.
   * Not used.
   */
  int log_level;

#ifndef __ASIX_C51__
  /** Length of configuration parameter ExtraClasses in zipgateway.cfg.
   * Non-secure part only.
   */
  u8_t extra_classes_len;
  /** Length of configuration parameter ExtraClasses in zipgateway.cfg.
   * Secure part only.
   */
  u8_t sec_extra_classes_len;
  /** Configuration parameter ExtraClasses in zipgateway.cfg.
   *
   * Non-secure extra command classes supported by the gateway application.
   * Secure and non-secure classes are separated by the "marker" 0xF100.
   */
  u8_t extra_classes[32];
  /** Configuration parameter ExtraClasses in zipgateway.cfg.
   *
   * Secure extra command classes supported by the gateway application.
   * Secure and non-secure classes are separated by the "marker" 0xF100.
   */
  u8_t sec_extra_classes[32];
#endif

  /*
   * Portal url
   */

  //const char* portal_url;
  /** Configuration parameter ZipPortal in zipgateway.cfg.
   * IP address of portal.
   */
  char portal_url[64];
  /** Configuration parameter ZipPortalPort in zipgateway.cfg.
   * Port number of portal.
   */
  u16_t portal_portno;

  /** Configuration parameter DebugZipIp4Disable in zipgateway.cfg.
   * Experimental feature.
   */
  u8_t ipv4disable;
  /** Command line parameter setting for debug purposes. */
  u8_t clear_eeprom;
  /** Configuration parameter ZipClientKeySize  in zipgateway.cfg.
   * Default 1024.
   */
  u16_t client_key_size;

  /** Configuration parameter ZipManufacturerID in zipgateway.cfg.
   *
   */
  u16_t manufacturer_id;
  /** Configuration parameter ZipProductType in zipgateway.cfg.
   *
   */
  u16_t product_type;
  /** Configuration parameter ZipProductID in zipgateway.cfg.
   *
   */
  u16_t product_id;
  /** Configuration parameter ZipHardwareVersion in zipgateway.cfg.
   *
   */
  u16_t hardware_version;
  /** Configuration parameter ZipDeviceID in zipgateway.cfg.
   *
   */ 
  char device_id[64];
  /** Length of configuration parameter ZipDeviceID in zipgateway.cfg.
   *
   */ 
  uint8_t device_id_len;
#ifndef __ASIX_C51__
  //certs info
  /** Configuration parameter ZipCaCert in zipgateway.cfg.
   * Path to certificate.
   */
  const char *ca_cert;
  /** Configuration parameter ZipCert in zipgateway.cfg.
   * Path to certificate.
   */
  const char *cert;
  /** Configuration parameter ZipPrivKey in zipgateway.cfg.
   * Path to SSL/DTLS private key.
   */
  const char *priv_key;
  /** Configuration parameter ZipPSK in zipgateway.cfg.
   *
   * DTLS key
   */
  char psk[64];
  /** Length of configuration parameter ZipPSK in zipgateway.cfg.
   *
   */
  u8_t psk_len;
#endif


  /** Configuration parameter ZipMBMode in zipgateway.cfg.
   *
   */
  mailbox_configuration_mode_t mb_conf_mode;

  /** Configuration parameter ZipMBDestinationIp6 in zipgateway.cfg.
   *
   * IP address of the mailbox proxy destination.
   */
  uip_ip6addr_t mb_destination;

  /** Configuration parameter ZipMBPort in zipgateway.cfg.
   *
   * Port of the mailbox proxy destination. Stored in
   * network byte order.
   * Default 41230.
   */
  uint16_t mb_port;

  //obsolete
  const char* echd_key_file;

  /** Whether the bridge controller supports smart start.
   * Auto-detected on start up.
   * True when smart start is enabled.
   */
  int enable_smart_start;

  /** Configuration parameter ZWRFRegion in zipgateway.cfg.
   *
   * RF region selection.
   * Only applies to 700-series chip.
   */
  u8_t rfregion;

  /** Configuration parameter NormalTxPowerLevel in zipgateway.cfg.
   *
   * Mark if powerlevel set in the zipgateway.cfg
   * Only applies to 700-series chip.
   */
  int is_powerlevel_set;

  /** Configuration parameters NormalTxPowerLevel, Measured0dBmPower
   * in zipgateway.cfg.
   *
   * TX powerlevel
   * Only applies to 700-series chip.
   */
  TX_POWER_LEVEL tx_powerlevel;
  /** Configuration parameters MaxLRTxPowerLevel
   * in zipgateway.cfg.
   *
   * MAX LR TX powerlevel
   * Only applies to 700-series chip.
   */
  int16_t max_lr_tx_powerlevel;

  /** Configuration parameter MaxLRTxPowerLevel in zipgateway.cfg.
   *
   * Mark if max lr powerlevel set in the zipgateway.cfg
   * Only applies to 700-series  an 800-series chip.
   */
  int is_max_lr_powerlevel_set;

  /** Configuration parameter ZWLBT in zipgateway.cfg.
   *
   *  sets the LBT Threshold anytime ZIPGW resets the Z-Wave chip. 
   *  ZW_SetListenBeforeTalkThreshold()
   */
  uint8_t zw_lbt;

  /** Configuration parameter single_classic_temp_association in zipgateway.cfg
   *
   * Allows to limit to a single classical temporal association. By default, the
   * Z/IP Gateway uses four classical temporal associations. If the parameter
   * `single_classic_temp_association` is set, the Z/IP Gateway will use
   * only one classical temporal associations.
   * Allowed values are 1=enable or 0=disable (default).
   * This is an experimental feature.
   */
  uint8_t single_classic_temp_association;
};

/**
 * When ever the struct zip_nvm_config, is changed the NVM config version should be incremented accordingly
 */
#define NVM_CONFIG_VERSION 2
typedef struct zip_nvm_config {
  u32_t magic;
  u16_t config_version;
  uip_lladdr_t mac_addr;

  /**
   * The ULA lan prefix. This is an autogenerated prefix, which is used if the
   * zipgateway.cfg ZipPanIp6 is set to null.
   */
  uip_ip6addr_t ula_pan_prefix;

  /**
   * The ULA lan prefix. This is an autogenerated ipaddress, which is used if the
   * zipgateway.cfg ZipLanIp6 is set to null.
   */
  uip_ip6addr_t ula_lan_addr;
  /** S0 security: if we have S0 granted key in gw, 1, else 0. */
  int8_t security_scheme;
  /* Security 0 key */
  u8_t security_netkey[16];
  /**  obsoleted setting */
  u8_t security_status;

  /**  obsoleted setting */
  mailbox_configuration_mode_t mb_conf_mode;
  /**  obsoleted setting */
  uip_ip6addr_t mb_destination;
  /**  obsoleted setting */
  uint16_t mb_port;

  /*S2 Network keys */
  u8_t assigned_keys; //Bitmask of keys which are assigned
  u8_t security2_key[3][16];
  u8_t ecdh_priv_key[32];
  u8_t security2_lr_key[2][16];
} CC_ALIGN_PACK zip_nvm_config_t ;
#define MAX_PEER_PROFILES 	1
#define GW_CONFIG_MAGIC  	0x434F4E46
#define ZIP_GW_MODE_STDALONE 	0x1
#define ZIP_GW_MODE_PORTAL		0x2

#define LOCK_BIT	0x0
#define ZIP_GW_LOCK_ENABLE_BIT	0x1
#define ZIP_GW_LOCK_SHOW_BIT	0x2
#define ZIP_GW_UNLOCK_SHOW_ENABLE 0x2

#define ZIP_GW_LOCK_SHOW_BIT_MASK 			0x3
#define ZIP_GW_LOCK_ENABLE_SHOW_DISABLE		0x1

#define ZWAVE_TUNNEL_PORT	44123

#define ZIPR_READY_OK   0xFF
#define INVALID_CONFIG	0x01

#define IPV6_ADDR_LEN	  16
#define IPV6_STR_LEN      64


typedef struct  _Gw_Config_St_
{
	u8_t mode;
	u8_t showlock;
	u8_t peerProfile;
	u8_t actualPeers;
}Gw_Config_St_t;

typedef struct  _Gw_PeerProfile_St_
{
	uip_ip6addr_t 	peer_ipv6_addr;
	u8_t 			port1;
	u8_t 			port2;
	u8_t	  		peerNameLength;
	char	  		peerName[63];
}Gw_PeerProfile_St_t;


typedef struct  _Gw_PeerProfile_Report_St_
{
	u8_t  			cmdClass;
	u8_t  			cmd;
	u8_t  			peerProfile;
	u8_t  			actualPeers;
	uip_ip6addr_t 	peer_ipv6_addr;
	u8_t 			port1;
	u8_t 			port2;
	u8_t	  		peerNameLength;
	char	  		peerName[63];

}Gw_PeerProfile_Report_St_t;


typedef struct  _Gw_Portal_Config_St_
{
	uip_ip6addr_t 	lan_ipv6_addr;
	u8_t  			lanPrefixLen;
	uip_ip6addr_t 	portal_ipv6_addr;
	u8_t  			portalPrefixLen;
	uip_ip6addr_t 	gw_ipv6_addr;
	uip_ip6addr_t 	pan_ipv6_addr;
	uip_ip6addr_t 	unsolicit_ipv6_addr;
	u8_t 			unsolicitPort1;
	u8_t 			unsolicitPort2;

}Gw_Portal_Config_St_t;

typedef struct portal_ip_configuration_with_magic_st
{
  u32_t magic;
  uip_ip6addr_t lan_address; //may be 0
  u8_t lan_prefix_length;

  uip_ip6addr_t tun_prefix;
  u8_t tun_prefix_length;

  uip_ip6addr_t gw_address; //default gw address, may be 0.

  uip_ip6addr_t pan_prefix; //pan prefix may be 0... prefix length is always /64

  uip_ip6addr_t unsolicited_dest; //unsolicited destination
  u16_t unsolicited_destination_port;
} CC_ALIGN_PACK portal_ip_configuration_with_magic_st_t;


typedef struct portal_ip_configuration_st
{
  uip_ip6addr_t lan_address; //may be 0
  u8_t lan_prefix_length;

  uip_ip6addr_t tun_prefix;
  u8_t tun_prefix_length;

  uip_ip6addr_t gw_address; //default gw address, may be 0.

  uip_ip6addr_t pan_prefix; //pan prefix may be 0... prefix length is always /64

  uip_ip6addr_t unsolicited_dest; //unsolicited destination
  u16_t unsolicited_destination_port;
} CC_ALIGN_PACK portal_ip_configuration_st_t;


typedef struct  application_nodeinfo_with_magic_st
{
  u32_t magic;
  u8_t nonSecureLen;
  u8_t secureLen;
  u8_t nonSecureCmdCls[64];
  u8_t secureCmdCls[64];
} CC_ALIGN_PACK application_nodeinfo_with_magic_st_t;

typedef struct  application_nodeinfo_st
{
  u8_t nonSecureLen;
  u8_t secureLen;
  u8_t nonSecureCmdCls[64];
  u8_t secureCmdCls[64];
} CC_ALIGN_PACK application_nodeinfo_st_t;

extern struct router_config cfg;

extern uint8_t gGwLockEnable;

#define ZIPMAGIC 1645985

#define nvm_config_get(par_name,dst) zw_appl_nvm_read(offsetof(zip_nvm_config_t,par_name),dst,sizeof(((zip_nvm_config_t*)0)->par_name))
#define nvm_config_set(par_name,src) zw_appl_nvm_write(offsetof(zip_nvm_config_t,par_name),src,sizeof(((zip_nvm_config_t*)0)->par_name))


typedef enum { FLOW_FROM_LAN, FLOW_FROM_TUNNEL} zip_flow_t;

#endif /* ZIP_ROUTER_CONFIG_H_ */
