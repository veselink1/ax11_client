#!/bin/bash
Xvfb :0 -screen 0 2340x1080x24 &
DISPLAY=:0 startxfce4 &
while true; do
  DISPLAY=:0 . build.sh
  echo "Failed! Restarting..."
done
