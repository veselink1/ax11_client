#!/bin/bash
pkill Xvfb
LD_PRELOAD="/home/veselin/src/android-shmem/libandroid-shmem-aarch64.so" Xvfb :0 -screen 0 1560x720x24 -shmem &
DISPLAY=:0 startxfce4 &
cd ~/src/ax11_client
while true; do
  DISPLAY=:0 . build.sh
  echo "Failed! Restarting..."
done
