import glob

import imageio
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

TEST_MAX = 1


def load_visualization(file: str):
    return np.array(imageio.imread(file), dtype=np.uint8)


def load_experiment(file: str):
    print('Loading data from file: ', file)
    return pd.read_csv("data/{0}".format(file))


def series_as_scalar(series: pd.Series):
    if len(series) != 1:
        RuntimeError('Attempted to conver series of length {} to scalar value'.format(len(series)))
    return series.iat[0]


def iterate_by_rank(reference_data: pd.DataFrame, function, skip):
    write_data = pd.DataFrame()
    for rank in reference_data['RANK'].unique():
        if rank in skip.values.tolist():
            continue

        def rank_filter(data: pd.DataFrame):
            return data[data['RANK'] == rank]

        write_data = function(rank_filter, write_data)
    return write_data


def dropIndex(data: pd.DataFrame):
    return data.reset_index(drop=True)


# Filter by event sequence number [min, max[
def filter_by_event_sequence_number(data: pd.DataFrame, min: pd.Series, max: pd.Series):
    return data[(data['EVENT_SEQ'] >= min) & (data['EVENT_SEQ'] < max)]


def calculate_runtime(data: pd.DataFrame, column='TIME'):
    eventInits = data[data['EVENT_TYPE'] == 'INIT-START']
    eventFinalizes = data[data['EVENT_TYPE'] == 'FINALIZE-STOP']
    return max(eventFinalizes[column]) - min(eventInits[column])


def calculate_restart_time(data1: pd.DataFrame, data2: pd.DataFrame):
    event2Inits = data2[data2['EVENT_TYPE'] == 'INIT-STOP']
    event1Failure = data1[data1['EVENT_TYPE'] == 'FAILURE-GENERATE']
    return max(event2Inits['WALLTIME']) - min(event1Failure['WALLTIME'])


def calculate_recovery_time(data: pd.DataFrame, column='TIME'):
    eventResume = data[data['EVENT_TYPE'] == 'RESTORE-STOP']
    eventFailure = data[data['EVENT_TYPE'] == 'FAILURE-GENERATE']
    return max(eventResume[column]) - min(eventFailure[column])


def calculate_checkpoint_time(data: pd.DataFrame, column='TIME'):
    eventResume = data[data['EVENT_TYPE'] == 'CHECKPOINT-STOP']
    eventFailure = data[data['EVENT_TYPE'] == 'CHECKPOINT-START']
    # TODO: This is a bit of a lie, max-max, as workaround to avoid data from different checkpoints
    return max(eventResume[column]) - max(eventFailure[column])


def export_stats(osu, jac2d, lulesh, file):
    data = [
        ['OSU', np.mean(osu), np.var(osu)],
        ['Jacobi', np.mean(jac2d), np.var(jac2d)],
        ['LULESH', np.mean(lulesh), np.var(lulesh)],
    ]
    frame = pd.DataFrame(data, columns=['Benchmark', 'Median', 'Variance'])
    frame.set_index('Benchmark', inplace=True)
    frame.to_csv(file)


def draw_runtime_boxplot(file_pattern="experiment_runtime_time_mpi_{0}_{1}_trace.csv",
                         title='Original Runtime of Benchmarks', csv='graphs/original-runtime-stats.csv',
                         pdf='graphs/original-runtime.pdf', time_column='TIME', evaluation_function=calculate_runtime):
    osu = []
    jac2d = []
    lulesh = []
    for experiment in range(0, TEST_MAX):
        osu.append(evaluation_function(load_experiment(file_pattern.format("osu", experiment)), time_column))
        jac2d.append(evaluation_function(load_experiment(file_pattern.format("jac2d", experiment)), time_column))
        lulesh.append(evaluation_function(load_experiment(file_pattern.format("lulesh", experiment)), time_column))

    data = [osu, jac2d, lulesh]
    export_stats(osu, jac2d, lulesh, csv)
    boxPlot(data, title, 'Runtime (s)', pdf)


def draw_scaling_runtime_boxplot(file_pattern, title, csv, pdf, pdf2, time_column='TIME',
                                 evaluation_function=calculate_runtime):
    data = []
    numProcesses = [6, 12, 24, 48, 96, 192, 384]
    for scale in numProcesses:
        entry = []
        for experiment in [0, 1, 2]:
            entry.append(evaluation_function(load_experiment(file_pattern.format(scale, experiment)), time_column))
        data.append(entry)

    def plotExpected():
        startVal = 3900
        data = [startVal / 3]
        for i in numProcesses:
            data.append(startVal / i)
        plt.plot(data)

    def plotExpectedLogScale():
        plotExpected()
        plt.yscale('log')

    boxPlot(data, title, 'Runtime (s)', pdf, xTickLabels=numProcesses, postProcess=lambda ax: plotExpected())
    boxPlot(data, title, 'Runtime (s)', pdf2, xTickLabels=numProcesses, postProcess=lambda ax: plotExpectedLogScale())


def draw_restart_boxplot(file_pattern="experiment_restart_time_mpi_{0}_{1}_trace.csv",
                         csv='graphs/restart-time-stats.csv', pdf='graphs/restart-time.pdf'):
    osu = []
    jac2d = []
    lulesh = []
    for experiment in range(0, TEST_MAX - 1):
        osu.append(calculate_restart_time(
            load_experiment(file_pattern.format("osu", experiment)),
            load_experiment(file_pattern.format("osu", experiment + 1))
        ))
        jac2d.append(calculate_restart_time(
            load_experiment(file_pattern.format("jac2d", experiment)),
            load_experiment(file_pattern.format("jac2d", experiment + 1))
        ))
        lulesh.append(calculate_restart_time(
            load_experiment(file_pattern.format("lulesh", experiment)),
            load_experiment(file_pattern.format("lulesh", experiment + 1))
        ))
    data = [osu, jac2d, lulesh]
    export_stats(osu, jac2d, lulesh, csv)
    boxPlot(data, 'Restart Time of Benchmarks', 'Time (s)', pdf)


def boxPlot(data, title, y_label, export, xTickLabels=None, postProcess=None):
    if xTickLabels is None:
        xTickLabels = ['OSU', 'Jacobi', 'LULESH']

    fig, ax = plt.subplots(figsize=(4, 2.5))
    ax.boxplot(data)
    ax.set_title(title)
    ax.set_xlabel('Benchmark')
    #    ax.set_xticks(np.arange(1,4))
    ax.set_xticklabels(xTickLabels)
    ax.set_ylabel(y_label)

    if postProcess is not None:
        postProcess(ax)

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


def calculate_restore_barchart(data: pd.DataFrame):
    failure_event = data[data['EVENT_TYPE'] == 'FAILURE-GENERATE']
    comm_errors = dropIndex(data[data['EVENT_TYPE'] == 'COMM-ERROR'])
    restore_start = dropIndex(data[data['EVENT_TYPE'] == 'RESTORE-START'])
    restore_stop = dropIndex(data[data['EVENT_TYPE'] == 'RESTORE-STOP'])

    def iterFunction(filter_by_rank, write_data):
        return write_data.append(filter_by_event_sequence_number(
            filter_by_rank(data[data['EVENT_TYPE'] == 'FAILURE-CHECK-START'].reset_index()).reset_index(),
            series_as_scalar(filter_by_rank(comm_errors)['EVENT_SEQ']),
            series_as_scalar(filter_by_rank(restore_stop)['EVENT_SEQ'])
        ))

    failure_check_starts = dropIndex(iterate_by_rank(data, iterFunction, skip=failure_event['RANK']))

    def iterFunction(filter_by_rank, write_data):
        return write_data.append(filter_by_event_sequence_number(
            filter_by_rank(data[data['EVENT_TYPE'] == 'FAILURE-CHECK-STOP'].reset_index()).reset_index(),
            series_as_scalar(filter_by_rank(comm_errors)['EVENT_SEQ']),
            series_as_scalar(filter_by_rank(restore_stop)['EVENT_SEQ'])
        ))

    failure_check_stops = dropIndex(iterate_by_rank(data, iterFunction, skip=failure_event['RANK']))

    plotData = pd.DataFrame([(
        np.average(comm_errors['WALLTIME'] - series_as_scalar(failure_event['WALLTIME'])),
        np.average(failure_check_starts['WALLTIME'] - comm_errors['WALLTIME']),
        np.average(failure_check_stops['WALLTIME'] - failure_check_starts['WALLTIME']),
        np.average(restore_start['WALLTIME'] - failure_check_stops['WALLTIME']),
        np.average(restore_stop['WALLTIME'] - restore_start['WALLTIME'])
    )], columns=['Propagate error', 'Wait for next detection', 'Detect', 'React', 'Restore'])

    ax = plotData.plot.barh(stacked=True)

    ax.set_title('Distribution of Activities during the Recovery Process')
    ax.set_xlabel('Time (s)')
    plt.show()

    ax.get_figure().savefig('graphs/recovery-time-distribution.pdf', format='pdf')
    plotData.set_index(pd.Series(['Time (s)'])).transpose().to_csv('graphs/recovery-time-distribution-stats.csv',
                                                                   index_label='Activities')

# draw_runtime_boxplot()
# draw_restart_boxplot()
# draw_restart_boxplot(file_pattern="experiment_restart_time_mca_mpi_{0}_{1}_trace.csv",
#                      csv='graphs/restart-time-mca-stats.csv',
#                      pdf='graphs/restart-time-mca.pdf')
# draw_runtime_boxplot(file_pattern='experiment_restart_time_to_solution_mpi_{0}_{1}_trace.csv',
#                      title='Measured Time to Solution Restart Strategy',
#                      csv='graphs/restart-time-to-solution-stats.csv',
#                      pdf='graphs/restart-time-to-solution.pdf',
#                      time_column='WALLTIME')
# draw_runtime_boxplot(file_pattern='experiment_checkpoint_time_to_solution_mpi_{0}_{1}_trace.csv',
#                      title='Measured Time to Solution Checkpoint Strategy',
#                      csv='graphs/checkpoint-time-to-solution-stats.csv',
#                      pdf='graphs/checkpoint-time-to-solution.pdf',
#                      time_column='WALLTIME')
# draw_runtime_boxplot(file_pattern='experiment_recovery_time_mpi_{0}_{1}_trace.csv',
#                      title='Time for Restoration in the Checkpoint Strategy',
#                      csv='graphs/checkpoint-restore-time-stats.csv',
#                      pdf='graphs/checkpoint-restore-time.pdf',
#                      time_column='WALLTIME',
#                      evaluation_function=calculate_recovery_time)
# draw_runtime_boxplot(file_pattern='experiment_recovery_time_mpi_{0}_{1}_trace.csv',
#                      title='Time for Checkpointing in the Checkpoint Strategy',
#                      csv='graphs/checkpoint-checkpoint-time-stats.csv',
#                      pdf='graphs/checkpoint-checkpoint-time.pdf',
#                      time_column='WALLTIME',
#                      evaluation_function=calculate_checkpoint_time)
# calculate_restore_barchart(load_experiment('experiment_recovery_time_mpi_jac2d_0_trace.csv'))

draw_scaling_runtime_boxplot(file_pattern='experiment_mpi_scale_jac2d_{0}_{1}_trace.csv',
                     title='Strong Scaling Test (Jac2D Benchmark)',
                     csv='graphs/scaling-jac2d-stats.csv',
                     pdf='graphs/scaling-jac2d.pdf',
                     pdf2='graphs/scaling-jac2d-log.pdf',
                     time_column='WALLTIME',
                     evaluation_function=calculate_runtime)


# draw_jac2d_example()
