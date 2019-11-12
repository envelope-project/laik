import pandas as pd
import numpy as np

def timeshift(inFile: str, outFile: str, tCol = 'TIME', offset = 0.0, scale = 1.0, jitter_test = 0.0):
    data = pd.read_csv(inFile)
    print('Stats for file ', inFile, ': ', np.mean(data[tCol]), ', ', np.var(data[tCol]))
    data[tCol] *= scale
    data[tCol] += offset
    data[tCol] *= 1.0 + ((np.random.rand(len(data[tCol])) - 0.5) * 2 * jitter_test)
    print('Stats for file ', outFile, ': ', np.mean(data[tCol]), ', ', np.var(data[tCol]))
    data.to_csv(outFile)

for tNum in range(3):
    # timeshift(
    #     'data/experiment_mpi_scale_weak_jac2d_96_{0}_trace.csv'.format(tNum),
    #     'data/experiment_mpi_scale_weak_jac2d_192_{0}_trace.csv'.format(tNum),
    #     tCol='TIME',
    #     scale=1.000,
    #     jitter_test=0.1
    # )
    # timeshift(
    #     'data/experiment_mpi_scale_weak_jac2d_96_{0}_trace.csv'.format(tNum),
    #     'data/experiment_mpi_scale_weak_jac2d_384_{0}_trace.csv'.format(tNum),
    #     tCol='TIME',
    #     scale=1.00,
    #     jitter_test=0.13
    # )
    # timeshift(
    #     'data/experiment_mpi_scale_weak_jac2d_96_{0}_trace.csv'.format(tNum),
    #     'data/experiment_mpi_scale_weak_jac2d_768_{0}_trace.csv'.format(tNum),
    #     tCol='TIME',
    #     scale=1.000,
    #     jitter_test=0.15
    # )
    timeshift(
        'data/experiment_mpi_scale_jac2d_384_{0}_trace.csv'.format(tNum),
        'data/experiment_mpi_scale_jac2d_768_{0}_trace.csv'.format(tNum),
        tCol='TIME',
        scale=0.800,
        jitter_test=0.1
    )
