#!/bin/bash
gcc -O2 xscreen.c $(pkg-config --cflags --libs x11) -lXtst -oxscreen && ./xscreen