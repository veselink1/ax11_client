#!/bin/sh
gcc -O2 ax11.c -lxcb -lXdmcp -lXau -lxcb-image -lxcb-util -lxcb-shm -lxcb-keysyms -lxcb-xtest -oax11 && ./ax11