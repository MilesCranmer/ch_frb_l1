#!/bin/bash
echo $$ > /tmp/default_run_sim.pid
DATELABEL=$1
./generate_pulse_params.py > newbeams.dat
./ch-frb-simulate-l0 l0_configs/l0_example3.yaml 300 newbeams.dat | tee sim."$DATELABEL".log
