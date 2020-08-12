#ifndef MESSAGE_SLOT_H
#define MESSAGE_SLOT_H

#include <linux/ioctl.h>

#define MAJOR_NUM 240

/* Set the message of the device driver */
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUM, 0, unsigned long)
#define MSG_MAX_LEN 128
#define CHAR_DEV_NAME "msgslot_char_dev"
#define DEVICE_FILE_NAME "msgslot"

#define SUCCESS 0

#endif

