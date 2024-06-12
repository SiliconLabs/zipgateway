/* © 2017 Silicon Laboratories Inc.
 */
/*
 * CC_ControllerInclusion.h
 *
 *  Created on: May 30, 2016
 *      Author: aes
 */

#ifndef SRC_CC_INCLUSIONCONTROLLER_H_
#define SRC_CC_INCLUSIONCONTROLLER_H_

#include<stdint.h>
typedef enum {
  INCLUSION_CONTROLLER_STEP_OK = 1,         /*The performed step was completed without error.*/
  INCLUSION_CONTROLLER_STEP_REJECTED,       /*The step was rejected by user*/
  INCLUSION_CONTROLLER_STEP_FAILED,         /*The step failed, because of a communication or protocol error.*/
  INCLUSION_CONTROLLER_STEP_NOT_SUPPORTED,  /*The step failed, because it Is not supported by the sending node.*/
} inclusion_cotroller_status_t;

typedef  void (*inclusion_controller_cb_t)(int status);
/**

 * Proxy inclusion frame flow
 *
 * \msc "S2 proxy inclusion"
 *  #The entries
 *
 *  ZC2 [ label ="User2" ],SIS,ZC1 [ label ="User1" ],IC [ label ="Incluison Controller" ],Node;
 *
 *  ZC1   >> IC   [ label = "Activate Add Mode" ];
 *  ZC1   >> Node   [ label = "Activate Learn Mode" ];
 *  SIS box Node  [ label = "Z-wave protocol inclusion" ];
 *  IC   -> SIS  [ label = "INITIATE, Proxy inclusion"];
 *  SIS  -> Node [ label = "Request node info" ];
 *  Node -> SIS  [ label = "Node Info" ];
 *  SIS  box ZC2  [ label = "S2 inclusion flow" ];
 *  SIS box Node [ label = "node probe" ];
 *  SIS >> ZC2  [ label = "done message" ];
 *  SIS -> IC    [ label = "COMPLETE, Proxy inclusion"];
 *  IC box Node  [ label = "node probe" ];
 *  IC >> ZC1   [ label = "done message" ];
 * \endmsc
 *
 * \msc "S0 proxy inclusion"
 *  #The entries
 *
 *  ZC2 [ label ="User2" ],SIS,ZC1 [ label ="User1" ],IC [ label ="Incluison Controller" ],Node;
 *
 *  ZC1   >> IC   [ label = "Activate Add Mode" ];
 *  ZC1   >> Node   [ label = "Activate Learn Mode" ];
 *  SIS box Node  [ label = "Z-wave protocol inclusion" ];
 *  IC   -> SIS  [ label = "INITIATE, Proxy inclusion"];
 *  SIS  -> Node [ label = "Request node info" ];
 *  Node -> SIS  [ label = "Node Info" ];
 *  SIS  -> IC   [ label = "INITIATE, S0 inclusion" ];
 *  IC box Node  [ label = "S0 inclusion"];
 *  IC -> SIS    [ label = "COMPLETE, S0 inclusion" ];
 *  SIS box Node [ label = "node probe" ];
 *  SIS >> ZC2  [ label = "done message" ];
 *  SIS -> IC    [ label = "COMPLETE, Proxy inclusion"];
 *  IC box Node  [ label = "node probe" ];
 *  IC >> ZC1   [ label = "done message" ];
 * \endmsc
 *
 * \msc "Non secure proxy inclusion"
 *  #The entries
 *
 *  ZC2 [ label ="User2" ],SIS,ZC1 [ label ="User1" ],IC [ label ="Incluison Controller" ],Node;
 *
 *  ZC1   >> IC   [ label = "Activate Add Mode" ];
 *  ZC1   >> Node   [ label = "Activate Learn Mode" ];
 *  SIS box Node  [ label = "Z-wave protocol inclusion" ];
 *  IC   -> SIS  [ label = "INITIATE, Proxy inclusion"];
 *  SIS  -> Node [ label = "Request node info" ];
 *  Node -> SIS  [ label = "Node Info" ];
 *  SIS box Node [ label = "node probe" ];
 *  SIS >> ZC2  [ label = "done message" ];
 *  SIS -> IC    [ label = "COMPLETE, Proxy inclusion"];
 *  IC box Node  [ label = "node probe" ];
 *  IC >> ZC1   [ label = "done message" ];
 * \endmsc
 *
 *
 *
 * Request controller inclusion from SIS
 *
 * @param node_id    node to reqeust for inclusion/replace
 * @param is_replace is this an inclusion or replace
 * @param complete_func callback when operation is done
 */

void request_inclusion_controller_handover( nodeid_t node_id,uint8_t is_replace,inclusion_controller_cb_t complete_func  );
/**
 * Must be called by SIS when is done processing the inclusion request
 */
void inclusion_controller_send_report(inclusion_cotroller_status_t status);

/**
 * Signal the the inclusion process has started, i.e. the user has accepted the first step in the inclusion
 */
void inclusion_controller_started(void);


/**
 * Inform inclusion controller to do the next step in the inclusion process.
 */
void inclusion_controller_you_do_it(inclusion_controller_cb_t complete_func) ;




#endif /* SRC_CC_CONTROLLERINCLUSION_H_ */
