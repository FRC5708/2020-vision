#!/bin/bash

# run this on startup (put a line like @reboot /path/to/this/script in crontab)
# relaunches vision code if it crashes

EXEC_PATH=/home/pi/bin/5708-vision

while [ true ]; do 

    # If someone's removed the executable, end the script
    if [ ! -f $EXEC_PATH ]; then exit; fi
        
        
    # If it's not running already, start it
    if ! killall -0 5708-vision ; then
        $EXEC_PATH
    fi
    sleep 1
    
done