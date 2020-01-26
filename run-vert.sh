#!/bin/bash
Xvfb :0 -screen 0 720x1560x24 &
DISPLAY=:0 startxfce4 &
while true; do
  DISPLAY=:0 . ~/src/ax11_client/build.sh
  echo "Failed! Restarting..."
done
