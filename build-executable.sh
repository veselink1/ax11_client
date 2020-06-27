#!/bin/bash
PWD=$(pwd)
musl-gcc ax11.c -O2 -g -Wall -static -pthread -I$PWD/include -Wl,-Bstatic -L$PWD/deps -lxcb -lXdmcp -lXau -lxcb-image -lxcb-util -lxcb-shm -lxcb-keysyms -lxcb-xtest -oax11 && ./ax11