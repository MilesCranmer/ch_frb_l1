#!/usr/bin/python

import numpy as np

# Want to randomize over log space to gauge behaviour
beams = 16
# 10 - 5000
DM = np.power(10, np.random.uniform(1.0, 3.7, beams))
# 3 - 90
width = np.power(3, np.random.uniform(1.0, 4.1, beams))
# 5 - 125
SNR = np.power(5, np.random.uniform(1.0, 3, beams))

for _ in range(3):
    for i in range(beams):
        print width[i], DM[i], SNR[i]
