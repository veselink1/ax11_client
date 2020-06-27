#!/bin/bash
pkill Xvfb
LD_PRELOAD=/home/veselin/src/android-shmem/libandroid-shmem-aarch64.so Xvfb :0 -screen 0 1280x720x24 -shmem &
env -u SESSION_MANAGER -u DBUS_SESSION_BUS_ADDRESS DISPLAY=:0 startxfce4 &
cd ~/src/ax11_client
while true; do
  LD_PRELOAD=/home/veselin/src/android-shmem/libandroid-shmem-aarch64.so DISPLAY=:0 AX11_SHMEM=1 . build-gcc.sh
  echo "Failed! Restarting..."
done
