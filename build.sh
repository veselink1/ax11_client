#!/bin/bash
PWD=$(pwd)
musl-gcc ax11.c -O2 -Wall -fPIC -ffunction-sections -funwind-tables -fstack-protector -shared -pthread -I$PWD/include -Wl,-Bstatic -L$PWD/deps -lxcb -lXdmcp -lXau -lxcb-image -lxcb-util -lxcb-shm -lxcb-keysyms -lxcb-xtest -olibax11.so