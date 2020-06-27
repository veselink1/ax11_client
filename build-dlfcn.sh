#!/bin/bash
PWD=$(pwd)
musl-gcc dlfcn.c -O0 -Wall -fPIC -ffunction-sections -funwind-tables -fstack-protector -shared -Wl,-Bstatic -olibdlfcn.so