#!/bin/bash
if [ -z "$PI_ADDR" ]; then PI_ADDR=team5708pi.local; fi
if [ -z "$PORT" ]; then PORT=22; fi

rsync -rc -e "ssh -p $PORT" `dirname $0`"/src" "pi@"$PI_ADDR":./vision-code/"
ssh pi@$PI_ADDR -p $PORT "cd ~/vision-code/src && make build -j4 && make install"
