#!/bin/bash
echo $$ > /tmp/default_run_l1.sh
nohup ./launch_l1.sh &
sleep 60
nohup ssh pipeline@frb-compute-0 'cd ~/miles/ch_frb_l1 && source activate chime-frb && timeout 800 ./default_run_sim.sh' &
i=0
while [ "$(cat /tmp/l1_launch_status.txt)" == "1" ]; do
    sleep 5
    echo "Waiting..."
    i=$((i+1))
    if [[ "$i" -gt 200 ]]; then
        echo "Broke"
        exit 0
    fi
done
echo "Finished iterations"
