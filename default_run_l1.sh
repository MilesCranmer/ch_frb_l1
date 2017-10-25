#!/bin/bash
echo $$ > /tmp/default_run_l1.pid
DATELABEL="$(date +'%H%M%S-%Y%m%d')"
./launch_l1.sh $DATELABEL &
sleep 60
ssh pipeline@frb-compute-0 "cd ~/miles/ch_frb_l1 && source activate chime-frb && timeout 800 ./default_run_sim.sh $DATELABEL" &
i=0
while [ "$(cat /tmp/l1_launch_status.txt)" == "1" ]; do
    sleep 5
    echo "Waiting..."
    i=$((i+1))
    if [[ "$i" -gt 180 ]]; then
        echo "Broke"
	kill -9 `cat /tmp/launch_l1.pid`
	ssh pipeline@frb-compute-0 'kill -9 `cat /tmp/default_run_sim.sh`'
        exit 0
    fi
done
echo "Finished iterations"
