/* © 2014 Silicon Laboratories Inc.
 */

#ifndef LINUX_USB_INTERFACE_H_
#define LINUX_USB_INTERFACE_H_

extern const struct zpg_interface linux_usb_interface;
/**
 * Detect if a given usb_device id existes
 */
int linux_usb_detect_by_id(int idVendor,int idProduct);

#endif /* LINUX_USB_INTERFACE_H_ */
