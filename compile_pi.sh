#!/bin/bash
if [ -z "$PI_ADDR" ]; then PI_ADDR=team5708pi.local; fi
if [ -z "$PORT" ]; then PORT=22; fi

rsync -rc -e "ssh -p $PORT" `dirname $0`"/src" "pi@"$PI_ADDR":./vision-code/$(git rev-parse --abbrev-ref HEAD)/"
ssh pi@$PI_ADDR -p $PORT "cd ~/vision-code/$(git rev-parse --abbrev-ref HEAD)/src && make build -j4 && make install"
