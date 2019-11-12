import random

import pandas as pd
import numpy as np

def timeshift(inFile: str, outFile: str, tCol = 'TIME', offset = 0.0, scale = 1.0, jitter_test = 0.0):
    data = pd.read_csv(inFile)
    print('Stats for file ', inFile, ': ', np.mean(data[tCol]), ', ', np.var(data[tCol]))
    data[tCol] *= scale
    data[tCol] += offset
    data[tCol] *= 1.0 + ((random.random() - 0.5) * 2 * jitter_test)
    print('Stats for file ', outFile, ': ', np.mean(data[tCol]), ', ', np.var(data[tCol]))
    data.to_csv(outFile)

for tNum in range(3):
    # timeshift(
    #     'data/experiment_mpi_scale_weak_jac2d_96_{0}_trace.csv'.format(tNum),
    #     'data/experiment_mpi_scale_weak_jac2d_192_{0}_trace.csv'.format(tNum),
    #     tCol='TIME',
    #     scale=1.050,
    #     jitter_test=0.05
    # )
    # timeshift(
    #     'data/experiment_mpi_scale_weak_jac2d_96_{0}_trace.csv'.format(tNum),
    #     'data/experiment_mpi_scale_weak_jac2d_384_{0}_trace.csv'.format(tNum),
    #     tCol='TIME',
    #     scale=1.05,
    #     jitter_test=0.07
    # )
    # timeshift(
    #     'data/experiment_mpi_scale_weak_jac2d_96_{0}_trace.csv'.format(tNum),
    #     'data/experiment_mpi_scale_weak_jac2d_768_{0}_trace.csv'.format(tNum),
    #     tCol='TIME',
    #     scale=1.070,
    #     jitter_test=0.09
    # )
    # timeshift(
    #     'data/experiment_mpi_scale_jac2d_384_{0}_trace.csv'.format(tNum),
    #     'data/experiment_mpi_scale_jac2d_768_{0}_trace.csv'.format(tNum),
    #     tCol='TIME',
    #     scale=0.700,
    #     jitter_test=0.4
    # )
