#!/bin/bash
if [ -z "$PI_ADDR" ]; then PI_ADDR=team5708pi.local; fi

rsync -rc -e "ssh -p 22" `dirname $0`"/src" "pi@"$PI_ADDR":./vision-code/"
ssh pi@$PI_ADDR -p 22 "cd ~/vision-code/src && make build -j4 && make install"
