import glob
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

def load_experiment(file : str):
    return pd.read_csv("data/{0}".format(file))

def calculate_runtime(data : pd.DataFrame):
    eventInits = data[data['EVENT_TYPE'] == 'INIT-START']
    eventFinalizes = data[data['EVENT_TYPE'] == 'FINALIZE-STOP']
    return max(eventFinalizes['TIME']) - min(eventInits['TIME'])

def calculate_restart_time(data1 : pd.DataFrame, data2 : pd.DataFrame):
    event2Inits = data2[data2['EVENT_TYPE'] == 'INIT-STOP']
    event1Failure = data1[data1['EVENT_TYPE'] == 'FAILURE-GENERATE']
    return max(event2Inits['WALLTIME']) - min(event1Failure['WALLTIME'])

def draw_runtime_boxplot():
    osu = []
    jac2d = []
    lulesh = []
    for experiment in range(0,9):
        osu.append(calculate_runtime(load_experiment("experiment_runtime_time_mpi_osu_{0}_trace.csv".format(experiment))))
        jac2d.append(calculate_runtime(load_experiment("experiment_runtime_time_mpi_jac2d_{0}_trace.csv".format(experiment))))
        lulesh.append(calculate_runtime(load_experiment("experiment_runtime_time_mpi_lulesh_{0}_trace.csv".format(experiment))))

    fig, ax = plt.subplots(figsize=(4,2.5))
    ax.boxplot([osu, jac2d, lulesh])
    ax.set_title('Original Runtime of Benchmarks')
    ax.set_xlabel('Benchmark')
#    ax.set_xticks(np.arange(1,4))
    ax.set_xticklabels(['OSU', 'Jacobi', 'LULESH'])
    ax.set_ylabel('Runtime (s)')

    plt.show()
    fig.savefig('graphs/original-runtime.pdf', format='pdf')

draw_runtime_boxplot()
