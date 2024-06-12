/* © 2014 Silicon Laboratories Inc.
 */


#include "zpgp.h"
#include <string.h>
#include <unistd.h>

/**
 * Wait for the FLASH FSM to report non busy
 */
static int wait_for_FSM(zw_pgmr_t* p) {
  int i;

  for(i=0; i< 64; i++) {
    if((zpgp_check_state(p) & 0x8) == 0) {
      return 0;
    }
    usleep(200*1000); /* wait 200 ms before checking again */
  }
  p->err("FLASH FSM stuck!\n");
  return -1;
}



int zpgp_enable_interface(zw_pgmr_t* p) {
  u8_t cmd[5];
  int i;
  cmd[0] =0xAC;
  cmd[1] =0x53;
  cmd[2] =0xAA;
  cmd[3] =0x55;

  p->xfer(cmd,4,4);

  for(i=0; i < 4; i++) {
    if(cmd[i] == 0xAC) {
      break;
    }
  }
  return i;
  /*return (cmd[2] ==0xAA && cmd[3] ==0x55 );*/
}


int zpgp_read_flash(zw_pgmr_t* p,u8_t sector) {
  u8_t cmd[4];

  cmd[0] =0x10;
  cmd[1] =sector;
  cmd[2] =0x0;
  cmd[3] =0x0;

  if( (p->xfer(cmd,4,4)!=4) ||
      cmd[0]!=0x10 || cmd[1] != sector || cmd[2]!=0x00 ){
    p->err("%s fail\n",__FUNCTION__);
    return -1;
  } else {
    return cmd[3];
  }
}

int zpgp_read_SRAM(zw_pgmr_t* p, u16_t addr) {
  u8_t cmd[4];
  cmd[0] =0x06;
  cmd[1] =(addr >> 8) & 0xff;
  cmd[2] =(addr >> 0) & 0xff;
  cmd[3] =0x0;

  if( (p->xfer(cmd,4,4)!=4) ||
      cmd[0]!=0x06 || cmd[1] != ((addr >> 8) & 0xFF) || cmd[2]!=((addr >> 0) & 0xFF)){
    p->err("%s fail\n",__FUNCTION__);
    return -1;
  }

  return cmd[3];
}


int zpgp_continue_read(zw_pgmr_t* p,u8_t* data,u8_t burst) {
  int i;
  u8_t cmd[4*burst];

  for(i=0; i < burst; i++) {
    cmd[4*i+0] =0xA0;
    cmd[4*i+1] =0x0;
    cmd[4*i+2] =0x0;
    cmd[4*i+3] =0x0;
  }

  if( p->xfer(cmd,4*burst,4*burst)!=4*burst )
  {
    p->err("%s fail\n",__FUNCTION__);
    return -1;
  } else {
    for(i=0; i < burst; i++) {
      if(cmd[4*i+0] != 0xA0) {
        p->err("%s fail\n",__FUNCTION__);
        return -1;
      }
      data[3*i+0] = cmd[4*i+1];
      data[3*i+1] = cmd[4*i+2];
      data[3*i+2] = cmd[4*i+3];
    }
  }
  return 0;
}

int zpgp_write_SRAM(zw_pgmr_t* p, u16_t addr, u8_t data) {
  u8_t cmd[4];

  cmd[0] =0x04;
  cmd[1] =(addr >> 8) & 0xFF;
  cmd[2] =(addr >> 0) & 0xFF;
  cmd[3] =data;

  if( (p->xfer(cmd,4,4)!=4) ||
      cmd[0]!=0x04 || cmd[1] != ((addr >> 8) & 0xFF) || cmd[2]!=((addr >> 0) & 0xFF) || cmd[3] != data){
    p->err("%s fail %.2x %.2x %.2x %.2x\n",__FUNCTION__,cmd[0],cmd[1],cmd[2],cmd[3]);
  }
  return 0;
}


int zpgp_continue_write(zw_pgmr_t* p, const u8_t* data,u8_t burst) {
  u8_t cmd[4*burst];
  int i;

  for(i=0; i < burst; i++) {
    cmd[4*i+0] =0x80;
    cmd[4*i+1] =data[3*i+0];
    cmd[4*i+2] =data[3*i+1];
    cmd[4*i+3] =data[3*i+2];
  }

  if( p->xfer(cmd,4*burst,4*burst)!=4*burst ){
    p->err("%s fail %x %x %x %x\n",__FUNCTION__,cmd[0],cmd[1],cmd[2],cmd[3]);
    return -1;
  } else {

    for(i=0; i < burst; i++) {
      if(cmd[4*i+0] !=0x80 ||
          cmd[4*i+1] !=data[3*i+0]||
          cmd[4*i+2] !=data[3*i+1]||
          cmd[4*i+3] !=data[3*i+2]) {
        return -1;
      }
    }

    return 0;
  }
}

int zpgp_erase_chip(zw_pgmr_t* p) {
  u8_t cmd[4];
  int rc = 0;

  cmd[0] =0x0A;
  cmd[1] =0x0;
  cmd[2] =0x0;
  cmd[3] =0x0;

  if( (p->xfer(cmd,4,4) !=4)||
      cmd[0]!=0x0A || cmd[1] != 0 || cmd[2]!=0 || cmd[3] != 0){
    p->err("%s fail\n",__FUNCTION__);
    rc = -1;
  }
  wait_for_FSM(p);
  return rc;
}


int zpgp_erase_sector(zw_pgmr_t* p,u8_t sector) {
  u8_t cmd[4];
  int rc = 0;

  cmd[0] =0x0B;
  cmd[1] =sector;
  cmd[2] =0x0;
  cmd[3] =0x0;

  if((p->xfer(cmd,4,4) !=4) ||
      cmd[0]!=0x0B || cmd[1] != sector || cmd[2]!=0 || cmd[3] != 0){
    p->err("%s fail\n",__FUNCTION__);
    rc = -1;
  }
  wait_for_FSM(p);
  return rc;
}



int zpgp_write_flash_sector(zw_pgmr_t* p,u8_t sector) {
  u8_t cmd[4];
  int rc = 0;

  cmd[0] =0x20;
  cmd[1] =sector;
  cmd[2] =0x0;
  cmd[3] =0x0;

  if( (p->xfer(cmd,4,4) !=4) ||
      cmd[0]!=0x20 || cmd[1] != sector || cmd[2]!=0 || cmd[3] != 0){
    p->err("%s fail\n",__FUNCTION__);
    rc = -1;
  }  
  wait_for_FSM(p);
  return rc;
}

int zpgp_check_state(zw_pgmr_t* p) {
  u8_t cmd[4];

  cmd[0] =0x7F;
  cmd[1] =0xFE;
  cmd[2] =0x0;
  cmd[3] =0x0;

  if( (p->xfer(cmd,4,4) !=4) ||
      cmd[0]!=0x7F || cmd[1] != 0xFE || cmd[2]!=0){
    p->err("%s fail\n",__FUNCTION__);
    return -1;
  } else {
    return cmd[3];
  }
}


int zpgp_read_signature_byte(zw_pgmr_t* p,u8_t num) {
  u8_t cmd[4];

  cmd[0] =0x30;
  cmd[1] = num;
  cmd[2] =0x0;
  cmd[3] =0x0;

  if( (p->xfer(cmd,4,4) !=4) ||
      cmd[0]!=0x30 || cmd[1] != num || cmd[2]!=0){
    p->err("%s fail\n",__FUNCTION__);
    return -1;
  } else {
    return cmd[3];
  }
}


int zpgp_zpgp_disable_EooS_mode(zw_pgmr_t* p) {
  u8_t cmd[4];

  cmd[0] =0xD0;
  cmd[1] =0x0;
  cmd[2] =0x0;
  cmd[3] =0x0;

  if( (p->xfer(cmd,4,4) !=4) ||
      cmd[0]!=0xD0 || cmd[1] != 0 || cmd[2]!=0 || cmd[3]!=0){
    p->err("%s fail\n",__FUNCTION__);
    return -1;
  } else {
    return 0;
  }
}


int zpgp_enable_EooS_mode(zw_pgmr_t* p) {
  u8_t cmd[4];

  cmd[0] =0xC0;
  cmd[1] =0x0;
  cmd[2] =0x0;
  cmd[3] =0x0;

  if((p->xfer(cmd,4,4) !=4) ||
      cmd[0]!=0xC0 || cmd[1] != 0 || cmd[2]!=0 || cmd[3]!=0){
    p->err("%s fail\n",__FUNCTION__);
    return -1;
  } else {
    return 0;
  }
}


int zpgp_set_lock_bits(zw_pgmr_t* p,lock_byte_t num, u8_t lock_data) {
  u8_t cmd[4];
  int rc = 0;

  cmd[0] =0xF0;
  cmd[1] =num;
  cmd[2] =0x0;
  cmd[3] =lock_data;

  if( (p->xfer(cmd,4,4) !=4) ||
      cmd[0]!=0xF0 || cmd[1] != num || cmd[2]!=0 || cmd[3]!=lock_data){
    p->err("%s fail\n",__FUNCTION__);
    rc = -1;
  }
  wait_for_FSM(p);
  return rc;
}

int zpgp_read_lock_bits(zw_pgmr_t* p,lock_byte_t num) {
  u8_t cmd[4];

  cmd[0] =0xF1;
  cmd[1] =num;
  cmd[2] =0x0;
  cmd[3] =0x0;

  if( (p->xfer(cmd,4,4) !=4) ||
      cmd[0]!=0xF1 || cmd[1] != num || cmd[2]!=0){
    p->err("%s fail\n",__FUNCTION__);
    return -1;
  } else {
    return cmd[3];
  }
}

int zpgp_set_nvr(zw_pgmr_t* p,u8_t addr, u8_t value) {
  u8_t cmd[4];

  cmd[0] =0xFE;
  cmd[1] =0x0;
  cmd[2] =addr;
  cmd[3] =value;

  if( (p->xfer(cmd,4,4) !=4) ||
      cmd[0]!=0xFE || cmd[1] != 0 || cmd[2]!=addr || cmd[3]!= value){
    p->err("%s fail\n",__FUNCTION__);
    return -1;
  } else {
    if (wait_for_FSM(p) >= 0) {
      return cmd[3];
    }
    else {
      p->err("%s non-fatal fail waiting for flash write, continuing...\n",__FUNCTION__);
      return cmd[3]; /* It is nonfatal for the flash FSM to get stuck, I have been told,
                        so we pretend everything is okay and continue. We have waited long enough
                        for the FLASH write to finish.  */
    }
  }
}

int zpgp_read_nvr(zw_pgmr_t* p,u8_t addr) {
  u8_t cmd[4];

  cmd[0] =0xF2;
  cmd[1] =0x0;
  cmd[2] =addr;
  cmd[3] =0x0;

  if( (p->xfer(cmd,4,4) !=4) ||
      cmd[0]!=0xF2 || cmd[1] != 0 || cmd[2]!=addr){
    p->err("%s fail\n",__FUNCTION__);
    return -1;
  } else {
    return cmd[3];
  }
}

/**
 * Run the CRC check procedure. Used to verify
 * that the correct data has been written to the
 * Flash.
 */
int zpgp_run_CRC_check(zw_pgmr_t* p) {
  u8_t cmd[4];

  cmd[0] =0xC3;
  cmd[1] =0x0;
  cmd[2] =0x0;
  cmd[3] =0x0;

  if( (p->xfer(cmd,4,4) !=4) ||
      cmd[0]!=0xC3 || cmd[1] != 0 || cmd[2]!=0x0 || cmd[3]!=0x0){
    p->err("%s fail\n",__FUNCTION__);
    return -1;
  } else {
    return cmd[3];
  }
}


/**
 * If the Auto Prog mode register bit is set then
 * the command clears the Auto Prog mode
 * register bit and resets the chip
 */
void zpgp_reset_chip(zw_pgmr_t* p) {
  u8_t cmd[4];

  cmd[0] =0xFF;
  cmd[1] =0xFF;
  cmd[2] =0xFF;
  cmd[3] =0xFF;

  p->xfer(cmd,4,0);
}

