# FRC Team 5708's camera and vision code for Infinite Recharge

Right now it's just a fork of the 2019 repo

## 	Streaming how to

Launch the start_steaming.sh script from a unix shell (WSL works). 
There are two environment variables: PI_ADDR and BITRATE (self-explanatory).
There's a bug where it will either exit or hang on launch and need to be relaunched.
If the first GStreamer window doesn't open within 10 seconds, kill it with ctrl-C and relaunch it.
The second window (if second camera is plugged in) takes longer to open. Wait at least 30 seconds for it.

## Overview

This program runs on a Raspberry Pi. It streams video from 2 cameras to the driver station, encoding it in H.264, which gives much higher resolution and framerate and lower bandwidth usage than the Motion JPEG which is usually used in FRC. It intercepts one of the video feeds to run vision processing on it (and draw feedback from the vision system on it).

And, of course, it's filled with caveats. Below is an overview of the implementation.

We utilize [gStreamer](https://gstreamer.freedesktop.org/) to encode and stream the video. It uses UDP, so the stream must be targeted at the driver station's IP address rather than connected to directly. In order to get the driver station's IP address, the driver station opens a connection to the raspberry pi on port 5807, and launches two gStreamer clients to start receiving the video streams. This program will then wait a second or two before launching gStreamer on the raspberry pi, as gStreamer must be open on the driver station before the first part of the stream is sent.

To intercept the video feed, a kernel module, [v4l2loopback](https://github.com/umlaeute/v4l2loopback) is needed. This program reads the video frames from the primary camera, copies it to run the vision processing asynchronously, draws the overlay on them, and writes them to the v4l2loopback device. gStreamer can then treat the v4l2loopback device like a (fake) camera. 

The secondary camera, if it exists, is given directly to gStreamer.

### Other caveats

**Color spaces:** The cameras and the video encoding both operate in the YUYV (or YCbCr, there's many names for it) color space. Frames are converted to RGB for vision processing, but for performance reasons, the overlay isn't, which limits it to various shades of green and pink and gives it colorful fringes.

**USB bandwidth:** The raspberry pi only has one USB controller, which means that all usb devices share a maximum of 480 mbps of bandwidth. The data is transmitted from the camera uncompressed, which eats this up quickly. Limiting the resolution to 800x448 with one camera or 640x360 each for two cameras gives a comfortable amount of headroom. 

**Firmware Update:** Using this code will not work stably without VL805 firmware 0137ad or higher -- https://www.raspberrypi.org/forums/viewtopic.php?t=260879
