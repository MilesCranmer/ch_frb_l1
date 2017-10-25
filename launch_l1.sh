#!/bin/bash
echo $$ > /tmp/launch_l1.pid
DATELABEL=$1
echo '1' > /tmp/l1_launch_status.txt
timeout 1000 ./ch-frb-l1 l1_configs/l1_example3.yaml rfi_configs/rfi_placeholder.json bonsai_configs/bonsai_noups_nbeta1.txt L1b_config.yaml 2>&1 | tee l1."$DATELABEL".log
echo '0' > /tmp/l1_launch_status.txt
