#!/bin/bash
make
sudo insmod message_slot.ko
sudo mknod /dev/my_msgslot c 240 0
sudo chmod o+rw /dev/my_msgslot
gcc -o message_sender{,.c}
gcc -o message_reader{,.c}
./message_sender /dev/my_msgslot 42 MESSAGE
./message_reader /dev/my_msgslot 42
sudo rmmod message_slot
make clean