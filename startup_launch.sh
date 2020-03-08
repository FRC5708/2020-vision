#!/bin/bash

# run on startup
# relaunches vision code if it crashes

EXEC_PATH=/home/pi/bin/5708-vision
TIMESTAMP="5708-vision-logs:$(date -Iseconds)"
ERROR_PATH="/tmp/$(echo $TIMESTAMP)/5708-vision-error.log"

mkdir /tmp/$(echo $TIMESTAMP)


while [ true ]; do 

    # If someone's removed the executable, end the script
    if [ ! -f $EXEC_PATH ]; then exit; fi
    # If it's not running already, start it
    if ! killall -0 5708-vision ; then
       $EXEC_PATH  2> $ERROR_PATH
    fi
    sleep 1
    
done

