#!/bin/bash
echo $$ > /tmp/default_run_sim.pid
./generate_pulse_params.py > newbeams.dt
./ch-frb-simulate-l0 l0_configs/l0_example3.yaml 300 newbeams.dat | tee sim.$(date +'%H%M%S-%Y%m%d').log
