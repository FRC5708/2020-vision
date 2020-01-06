#!/bin/sh

if [ -z "$BITRATE" ]; then BITRATE=1000000; fi

# Collect any residual packets in the queue before launching GStreamer
timeout 1 nc -ul4 5809 &
timeout 1 nc -ul6 5809 &

if [ -z "$GST_COMMAND" ]; then 
    if [ -f /proc/version ] && grep -q "Microsoft" /proc/version; then
        GST_COMMAND="/mnt/c/gstreamer/1.0/x86_64/bin/gst-launch-1.0.exe"
        if [ -z "$PI_ADDR" ]; then PI_ADDR=10.57.8.5; fi
    else
        GST_COMMAND="gst-launch-1.0"
        if [ -z "$PI_ADDR" ]; then PI_ADDR=raspberrypi.local; fi
    fi
fi

if echo $BITRATE | nc  $PI_ADDR 5807; then
    $GST_COMMAND udpsrc port=5809 ! gdpdepay ! rtph264depay ! avdec_h264 ! autovideosink sync=false #&
    #$GST_COMMAND udpsrc port=5804 ! gdpdepay ! rtph264depay ! avdec_h264 ! autovideosink sync=false
else
    echo "Could not connect to rPi"
    exit 1
fi
