#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "message_slot.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        errno = EINVAL;
        perror(NULL);
        exit(EXIT_FAILURE);
    }
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open failed");
        exit(EXIT_FAILURE);
    }
    unsigned int channel = (unsigned int) atoi(argv[2]);
    if (ioctl(fd, MSG_SLOT_CHANNEL, channel)) {
        perror("ioctl failed");
        exit(EXIT_FAILURE);
    }
    char buffer[MSG_MAX_LEN];
    ssize_t bytes_read = read(fd, buffer, MSG_MAX_LEN);
    if (bytes_read < 0) {
        perror("read failed");
        exit(EXIT_FAILURE);
    }
    if (write(STDOUT_FILENO, buffer, (size_t) bytes_read) < 0) {
        perror("write to stdout failed");
        exit(EXIT_FAILURE);
    }
    if (close(fd)) {
        perror("close failed");
        exit(EXIT_FAILURE);
    }
    return EXIT_SUCCESS;
}

