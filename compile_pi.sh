#!/bin/bash
if [ -z "$PI_ADDR" ]; then PI_ADDR=raspberrypi.local; fi

#PI_DIR="~/vision-code"
PI_DIR="~/vision-code-test"

rsync -rc -e "ssh -p 5810" `dirname $0`"/src" "pi@"$PI_ADDR:$PI_DIR
#add the sample images to the pi
rsync -rc -e "ssh -p 5810" `dirname $0`"/sampleImgs" "pi@"$PI_ADDR:$PI_DIR
ssh pi@$PI_ADDR -p 5810 "cd "$PI_DIR"/src && make build -j4 && make install"
