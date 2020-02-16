#!/bin/sh
# Tunnels ports needed for streaming
# Must be started before there is any udp traffic i.e. it must be restarted before every time gstreamer is launched
# e.g. this order: killall gst-launch-1.0, launch picode, run this script, run start_streaming.sh
ssh -L 5807:localhost:5807 -R 5809:localhost:5809 pi@max-vpn.us.to -p 2222 #"killall naudpl; ~/bin/naudpl 5809" | nc -u4 localhost 5809
