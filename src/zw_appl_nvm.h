/* © 2019 Silicon Laboratories Inc. */

#ifdef NO_ZW_NVM
void zw_appl_nvm_init(void);
void zw_appl_nvm_close(void);
#endif

extern int zw_appl_nvm_read(u16_t start,void* dst,u8_t size);
extern void zw_appl_nvm_write(u16_t start,const void* dst,u8_t size);
