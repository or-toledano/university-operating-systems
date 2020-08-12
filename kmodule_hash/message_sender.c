#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include "message_slot.h"

int main(int argc, char *argv[]) {
    if (argc != 4) {
        errno = EINVAL;
        perror(NULL);
        exit(EXIT_FAILURE);
    }
    int fd = open(argv[1], O_WRONLY);
    if (fd < 0) {
        perror("open failed");
        exit(EXIT_FAILURE);
    }
    unsigned int channel = (unsigned int) atoi(argv[2]);
    if (ioctl(fd, MSG_SLOT_CHANNEL, channel)) {
        perror("ioctl failed");
        exit(EXIT_FAILURE);
    }
    if (write(fd, argv[3], strlen(argv[3])) < 0) {
        perror("write failed");
        exit(EXIT_FAILURE);
    }
    if (close(fd)) {
        perror("close failed");
        exit(EXIT_FAILURE);
    }
    return EXIT_SUCCESS;
}

