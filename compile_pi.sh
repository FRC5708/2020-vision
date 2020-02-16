#!/bin/bash
if [ -z "$PI_ADDR" ]; then PI_ADDR=team5708pi.local; fi
if [ -z "$PORT" ]; then PORT=22; fi

set -e

PI_DIR="vision-code/$(git rev-parse --abbrev-ref HEAD)"
CODE_DIR=$(dirname $0)

set -x

rsync -rc -e "ssh -p $PORT" "$CODE_DIR/src" $CODE_DIR"/sampleImgs" "pi@"$PI_ADDR":./$PI_DIR/"
ssh pi@$PI_ADDR -p $PORT "cd ~/$PI_DIR/src && make build -j4 && make install"
