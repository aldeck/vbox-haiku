#ifndef _DEBUG_X_H
#define _DEBUG_X_H

void dump_hex(uchar *data, int len, char *prefix);
void dump_link( link_pointer_t *link, char *prefix);
void dump_td( td_t *td, char *prefix);
void dump_queue_head( queue_head_t *qh, char *prefix);
void dump_transaction( transaction_t *trans, char *prefix);
void dump_usbdev( usbdev_t *dev, char *prefix);
void dump_uhci(uint32_t port);
//void dump_all_usbdev(char *prefix);
void dump_device_descriptor( device_descriptor_t *des, char *prefix);
void dump_interface_descriptor( interface_descriptor_t *iface, char *prefix);
void dump_endpoint_descriptor( endpoint_descriptor_t *ep, char *prefix);
void dump_config_descriptor( uchar *des, char *prefix);
void dump_ctrlmsg( ctrl_msg_t *msg, char *prefix);

#endif
