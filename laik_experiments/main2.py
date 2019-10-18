import glob

import imageio
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

def load_visualization(file : str):
    return np.array(imageio.imread(file), dtype=np.uint8)

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

def export_stats(osu, jac2d, lulesh, file):
    data = [
        ['OSU', np.mean(osu), np.var(osu)],
        ['Jacobi', np.mean(jac2d), np.var(jac2d)],
        ['LULESH', np.mean(lulesh), np.var(lulesh)],
    ]
    frame = pd.DataFrame(data, columns=['Benchmark', 'Median', 'Variance'])
    frame.set_index('Benchmark', inplace=True)
    frame.to_csv(file)

def draw_runtime_boxplot():
    osu = []
    jac2d = []
    lulesh = []
    for experiment in range(0,9):
        osu.append(calculate_runtime(load_experiment("experiment_runtime_time_mpi_osu_{0}_trace.csv".format(experiment))))
        jac2d.append(calculate_runtime(load_experiment("experiment_runtime_time_mpi_jac2d_{0}_trace.csv".format(experiment))))
        lulesh.append(calculate_runtime(load_experiment("experiment_runtime_time_mpi_lulesh_{0}_trace.csv".format(experiment))))

    data = [osu, jac2d, lulesh]
    export_stats(osu, jac2d, lulesh, 'graphs/original-runtime-stats.csv')
    boxPlot(data, 'Original Runtime of Benchmarks', 'Runtime (s)', 'graphs/original-runtime.pdf')

def draw_restart_boxplot(file_pattern="experiment_restart_time_mpi_{0}_{1}_trace.csv",
                         csv='graphs/restart-time-stats.csv', pdf='graphs/restart-time.pdf'):
    osu = []
    jac2d = []
    lulesh = []
    for experiment in range(0,8):
        osu.append(calculate_restart_time(
            load_experiment(file_pattern.format("osu",experiment)),
            load_experiment(file_pattern.format("osu",experiment+1))
        ))
        jac2d.append(calculate_restart_time(
            load_experiment(file_pattern.format("jac2d",experiment)),
            load_experiment(file_pattern.format("jac2d",experiment+1))
        ))
        lulesh.append(calculate_restart_time(
            load_experiment(file_pattern.format("lulesh",experiment)),
            load_experiment(file_pattern.format("lulesh", experiment + 1))
        ))
    data = [osu, jac2d, lulesh]
    export_stats(osu, jac2d, lulesh, csv)
    boxPlot(data, 'Restart Time of Benchmarks', 'Time (s)', pdf)


def boxPlot(data, title, y_label, export):
    fig, ax = plt.subplots(figsize=(4, 2.5))
    ax.boxplot(data)
    ax.set_title(title)
    ax.set_xlabel('Benchmark')
    #    ax.set_xticks(np.arange(1,4))
    ax.set_xticklabels(['OSU', 'Jacobi', 'LULESH'])
    ax.set_ylabel(y_label)
    plt.show()
    fig.savefig(export, format='pdf')


def draw_jac2d_example():
    fig, axs = plt.subplots(1, 5, figsize=(6, 2))
    # plt.subplots_adjust(left=0.125, right=0.9, top=0.9, bottom=0.0, wspace=0.1, hspace=0.0)
    for i in range(5):
        axs[i].imshow(load_visualization('data/jac2d-example/data_dW_{0}_0.ppm'.format(i * 5)), origin='lower')
        axs[i].set_title('Iter {0}'.format(i * 5))
    fig.suptitle('Iterations of the Jacobi 2D Heat Diffusion Simulation')
    plt.show()
    fig.savefig('graphs/jac2d-example.pdf', format='pdf')


draw_runtime_boxplot()
draw_restart_boxplot()
draw_restart_boxplot(file_pattern="experiment_restart_time_mpi_{0}_mca_{1}_trace.csv",
                     csv='graphs/restart-time-mca-stats.csv',
                     pdf='graphs/restart-time-mca.pdf')
draw_jac2d_example()
