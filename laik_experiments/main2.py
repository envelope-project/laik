import glob

import imageio
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

TEST_MAX = 0

arrowprops = dict(
    arrowstyle="->",
    connectionstyle="angle,angleA=0,angleB=90,rad=10")


def load_visualization(file: str):
    return np.array(imageio.imread(file), dtype=np.uint8)


def load_experiment(file: str):
    print('Loading data from file: ', file)
    return pd.read_csv("data/{0}".format(file))


def series_as_scalar(series: pd.Series):
    if len(series) != 1:
        RuntimeError('Attempted to conver series of length {} to scalar value'.format(len(series)))
    return series.iat[0]


def iterate_by_rank(reference_data: pd.DataFrame, function, skip=None):
    if(skip is None):
        skip = pd.DataFrame()
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
    frame = pd.DataFrame(data, columns=['Benchmark', 'Average', 'Variance'])
    frame.set_index('Benchmark', inplace=True)
    frame.to_csv(file)


def draw_runtime_boxplot(file_pattern="experiment_runtime_time_mpi_{0}_{1}_trace.csv",
                         title='Original Runtime of Benchmarks', csv='graphs/original-runtime-stats.csv',
                         pdf='graphs/original-runtime.pdf',
                         time_column='TIME',
                         evaluation_function=calculate_runtime,
                         scatter=False):
    osu = []
    jac2d = []
    lulesh = []
    scatterData = pd.DataFrame(columns=['x', 'y', 'numfails'])

    for experiment in range(0, TEST_MAX):
        evaluateData("osu", evaluation_function, file_pattern, experiment, osu, time_column, scatterData)
        evaluateData("jac2d", evaluation_function, file_pattern, experiment, jac2d, time_column, scatterData)
        evaluateData("lulesh", evaluation_function, file_pattern, experiment, lulesh, time_column, scatterData)

    data = [osu, jac2d, lulesh]
    export_stats(osu, jac2d, lulesh, csv)

    def postProcess(ax: plt.Axes):
        if scatter:
            ax.get_figure().set_size_inches(6, 2.5)
            box = ax.get_position()
            ax.set_position([box.x0, box.y0, box.width * 1.25, box.height])
            scatters = []
            scatterLabels = []
            unique = scatterData['numfails'].unique()
            unique.sort()
            colors=['green', 'blue', 'orange', 'red', 'purple']
            for i in unique:
                row = scatterData[scatterData['numfails'] == i]
                scatters.append(ax.scatter(row['x'] + 0.25, row['y'], c=colors[int(i)]))
                scatterLabels.append('{} failures'.format(int(i)))
            plt.legend(scatters, scatterLabels, loc='center left', bbox_to_anchor=(1, 0.5))

    boxPlot(data, title, 'Runtime (s)', pdf, postProcess=postProcess)

def draw_runtime_boxplot_2(file_pattern="experiment_runtime_time_mpi_{0}_{1}_trace.csv",
                           file_pattern_2="",
                         title='Original Runtime of Benchmarks', csv='graphs/original-runtime-stats.csv',
                         pdf='graphs/original-runtime.pdf',
                         time_column='TIME',
                         evaluation_function=calculate_runtime,
                         scatter=False):
    osu = []
    jac2d = []
    lulesh = []
    osu2 = []
    jac2d2 = []
    lulesh2 = []
    scatterData = pd.DataFrame(columns=['x', 'y', 'numfails'])

    for experiment in range(0, TEST_MAX):
        evaluateData("osu", evaluation_function, file_pattern, experiment, osu, time_column, scatterData)
        evaluateData("jac2d", evaluation_function, file_pattern, experiment, jac2d, time_column, scatterData)
        evaluateData("lulesh", evaluation_function, file_pattern, experiment, lulesh, time_column, scatterData)

    for experiment in range(0, TEST_MAX):
        evaluateData("osu", evaluation_function, file_pattern_2, experiment, osu2, time_column, scatterData, xVal=4)
        evaluateData("jac2d", evaluation_function, file_pattern_2, experiment, jac2d2, time_column, scatterData, xVal=5)
        # evaluateData("lulesh", evaluation_function, file_pattern, experiment, lulesh2, time_column, scatterData)

    data = [osu, jac2d, lulesh, osu2, jac2d2, lulesh2]
    # export_stats(osu, jac2d, lulesh, csv)

    def postProcess(ax: plt.Axes):
        if scatter:
            # ax.get_figure().set_size_inches(6, 2.5)
            box = ax.get_position()
            ax.set_position([box.x0, box.y0, box.width * 1.0, box.height])
            scatters = []
            scatterLabels = []
            unique = scatterData['numfails'].unique()
            unique.sort()
            colors=['green', 'blue', 'orange', 'red', 'purple']
            for i in unique:
                row = scatterData[scatterData['numfails'] == i]
                scatters.append(ax.scatter(row['x'] + 0.4, row['y'], c=colors[int(i)]))
                scatterLabels.append('{} failures'.format(int(i)))
            plt.legend(scatters, scatterLabels, loc='center left', bbox_to_anchor=(1, 0.5))

    boxPlot(data, title, 'Runtime (s)', pdf, postProcess=postProcess, figsize=(7, 2.5), xTickLabels=["OSU", "Jac2D", "LULESH", "OSU-FT", "Jac2D-FT", "LULESH-FT"])



def evaluateData(name, evaluation_function, file_pattern, experiment, data, time_column, scatterData : pd.DataFrame, xVal = -1):
    experimentData = load_experiment(file_pattern.format(name, experiment))
    value = evaluation_function(experimentData, time_column)
    data.append(value)
    if xVal == -1:
        if name == 'osu':
            xVal = 1
        elif name == 'jac2d':
            xVal = 2
        elif name == 'lulesh':
            xVal = 3
    scatterData.loc[len(scatterData.index)] = [xVal, value, count_failures(experimentData)]


def count_failures(experimentData):
    return len(experimentData[experimentData['EVENT_TYPE'] == 'FAILURE-GENERATE'])


def draw_scaling_runtime_boxplot(file_pattern, title, csv, pdf, pdf2, time_column='TIME',
                                 evaluation_function=calculate_runtime, num_processes=None, include_log_graph=False, suppress_expected=False, suppress_weak_expected=True):
    if num_processes is None:
        num_processes = [6, 12, 24, 48, 96, 192, 384]
    data = []
    for scale in num_processes:
        entry = []
        for experiment in range(TEST_MAX):
            entry.append(evaluation_function(load_experiment(file_pattern.format(scale, experiment)), time_column))
        data.append(entry)

    def plotExpected():
        startVal = 3900
        data = [startVal / 3]
        data2 = [startVal / (30 * np.power(3, 0.15))]
        for i in num_processes:
            data.append(startVal / i)
            data2.append(startVal / (30 * np.power(i, 0.15)))
        if not suppress_expected:
            plt.plot(data)
            plt.plot(data2)
        if not suppress_weak_expected:
            print('Axhline')
            plt.plot([0.5, 4.5], [80, 80])
            plt.plot([4.5, 8.5], [110, 110])
        plt.xlabel('Number of processes')

    def plotExpectedLogScale():
        plotExpected()
        plt.yscale('log')

    boxPlot(data, title, 'Runtime (s)', pdf, xTickLabels=num_processes, postProcess=lambda ax: plotExpected())
    if include_log_graph:
        boxPlot(data, title, 'Runtime (s)', pdf2, xTickLabels=num_processes, postProcess=lambda ax: plotExpectedLogScale(), ylim=[None, None])


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

def draw_memory_plot(file_pattern='experiment_demo_jac2d_0_trace.csv'):
    data = load_experiment(file_pattern)
    iterate_by_rank(data, lambda rank_filter, y: plt.plot(rank_filter(data)['TIME'], rank_filter(data)['MEM']))

    plt.show()

def draw_memory_plot_2(file_pattern='demo_mem/mem{0}.csv', pdf='graphs/demo-mem.pdf'):
    fig = plt.figure()
    annotate_offset=50
    bbox = dict(boxstyle="round", fc="1.0")

    for i in range(4):
        data = load_experiment(file_pattern.format(i))
        plt.step(data['TIME'] / 1000, data['MEM'])
        if i == 1:
            maxTime = max(data['TIME'])
            plt.annotate('Failure',
                         (maxTime / 1000, series_as_scalar(data[data['TIME'] == maxTime].loc[:,'MEM'])),
                         xytext=(0, -0.75 * annotate_offset),
                         textcoords='offset points',
                         horizontalalignment='center',
                         bbox=bbox,
                         arrowprops=arrowprops
                         )
    plt.annotate('Unprotected',
             (4.5, 75000000),
             xytext=(0, -0.9 * annotate_offset),
             textcoords='offset points',
             horizontalalignment='center',
             bbox=bbox,
             arrowprops=arrowprops
             )
    plt.annotate('Checkpoint',
             (6.5, 210000000),
             xytext=(0, annotate_offset),
             textcoords='offset points',
             horizontalalignment='center',
             bbox=bbox,
             arrowprops=arrowprops
             )
    plt.annotate('Restore',
             (10.75, 145000000),
             xytext=(0, -1.2*annotate_offset),
             textcoords='offset points',
             horizontalalignment='center',
             bbox=bbox,
             arrowprops=arrowprops
             )
    annotate_set_sizes(bbox)

    plt.xlabel('Time (s)')
    plt.ylabel('Memory Consumption (byte)')
    plt.title('Memory Consumption (Jacobi, Early Release)')

    plt.legend(['Rank 0', 'Rank 1', 'Rank 2', 'Rank 3'])

    plt.show()

    fig.savefig(pdf, format='pdf')


def annotate_set_sizes(bbox):
    x = 21
    plt.xlim([0, 24])
    plt.ylim([0, 380000000])
    plt.annotate('Data Set Size',
                 (x, 134217728),
                 horizontalalignment='center',
                 verticalalignment='center',
                 bbox=bbox,
                 )
    plt.axhline(134217728, linestyle='--')
    plt.annotate('1/3 Data Set Size',
                 (x, 44739243 + 5000000),
                 horizontalalignment='center',
                 verticalalignment='center',
                 bbox=bbox,
                 )
    plt.axhline(44739243, linestyle='--')
    plt.annotate('1/4 Data Set Size',
                 (x, 33554432 - 5000000),
                 horizontalalignment='center',
                 verticalalignment='center',
                 bbox=bbox,
                 )
    plt.axhline(33554432, linestyle='--')


def draw_memory_plot_3(file_pattern='demo_mem/mem{0}.csv', pdf='graphs/demo-mem.pdf'):
    fig = plt.figure()
    annotate_offset=50
    bbox = dict(boxstyle="round", fc="1.0")

    for i in range(4):
        data = load_experiment(file_pattern.format(i))
        plt.step(data['TIME'] / 1000, data['MEM'])
        if i == 1:
            maxTime = max(data['TIME'])
            plt.annotate('Failure',
                         (maxTime / 1000, series_as_scalar(data[data['TIME'] == maxTime].loc[:,'MEM'])),
                         xytext=(0, -0.75 * annotate_offset),
                         textcoords='offset points',
                         horizontalalignment='center',
                         bbox=bbox,
                         arrowprops=arrowprops
                         )
    plt.annotate('Unprotected',
             (4, 75000000),
             xytext=(0, 1 * annotate_offset),
             textcoords='offset points',
             horizontalalignment='center',
             bbox=bbox,
             arrowprops=arrowprops
             )
    plt.annotate('Checkpoint',
             (6.5, 210000000),
             xytext=(0, annotate_offset),
             textcoords='offset points',
             horizontalalignment='center',
             bbox=bbox,
             arrowprops=arrowprops
             )
    plt.annotate('Restore',
             (10.35, 150000000),
             xytext=(0, -1.3*annotate_offset),
             textcoords='offset points',
             horizontalalignment='center',
             bbox=bbox,
             arrowprops=arrowprops
             )
    annotate_set_sizes(bbox)

    plt.xlabel('Time (s)')
    plt.ylabel('Memory Consumption (byte)')
    plt.title('Memory Consumption (Jacobi, Standard Release)')

    plt.legend(['Rank 0', 'Rank 1', 'Rank 2', 'Rank 3'])

    plt.show()

    fig.savefig(pdf, format='pdf')


def draw_network_plot():
    fig = plt.figure()
    annotate_offset=50
    bbox = dict(boxstyle="round", fc="1.0")

    data = load_experiment('experiment_demo_jac2d_1_trace.csv')

    def process(rank_filter, write):
        filterData = rank_filter(data)
        filterData.loc[:, 'DIFF'] = filterData['NET'].diff()
        for index, i in filterData.iterrows():
            if i['DIFF'] < 0:
                filterData.loc[filterData['EVENT_SEQ'] >= i['EVENT_SEQ'], 'NET'] -= i['DIFF']
        plt.step(filterData['TIME'], filterData['NET'] * 1024)

    iterate_by_rank(data, process)

    plt.annotate('Failure',
                 (6.5, 67000000),
                 xytext=(0, -0.5 * annotate_offset),
                 textcoords='offset points',
                 horizontalalignment='center',
                 bbox=bbox,
                 arrowprops=arrowprops
                 )
    plt.annotate('Checkpoint',
             (3.5, 67000000),
             xytext=(0, annotate_offset),
             textcoords='offset points',
             horizontalalignment='center',
             bbox=bbox,
             arrowprops=arrowprops
             )
    plt.annotate('Restore',
             (8.3, 135000000),
             xytext=(0, 1.7*annotate_offset),
             textcoords='offset points',
             horizontalalignment='center',
             bbox=bbox,
             arrowprops=arrowprops
             )

    annotate_set_sizes(bbox)

    plt.xlabel('Time (s)')
    plt.ylabel('Communicated Data (bytes)')
    plt.title('Data Transmitted in the Jacobi Benchmark')

    plt.legend(['Rank 0', 'Rank 1', 'Rank 2', 'Rank 3'])

    plt.show()

    fig.savefig('graphs/demo-net.pdf', format='pdf')


def boxPlot(data, title, y_label, export, xTickLabels=None, postProcess=None, ylim=None, figsize=(4, 2.5)):
    if ylim is None:
        ylim = [0, None]
    if xTickLabels is None:
        xTickLabels = ['OSU', 'Jacobi', 'LULESH']

    fig, ax = plt.subplots(figsize=figsize)
    ax.boxplot(data)
    ax.set_ylim(ylim)
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


def draw_jac2d_example_2():
    # plt.subplots_adjust(left=0.125, right=0.9, top=0.9, bottom=0.0, wspace=0.1, hspace=0.0)
    for i in range(5):
        fig = plt.figure(figsize=(2, 2))
        ax = plt.gca()
        ax.imshow(load_visualization('data/jac2d-example/data_dW_{0}_0.ppm'.format(i * 5)), origin='lower')
        ax.set_title('Iter {0}'.format(i * 5))
        plt.show()
        fig.savefig('graphs/jac2d-example-iter-{0}.pdf'.format(i), format='pdf')


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

    labels = ['Propagate error', 'Wait for detection', 'Detect', 'React', 'Restore']
    plotData = pd.DataFrame([(
        np.average(comm_errors['WALLTIME'] - series_as_scalar(failure_event['WALLTIME'])),
        np.average(failure_check_starts['WALLTIME'] - comm_errors['WALLTIME']),
        np.average(failure_check_stops['WALLTIME'] - failure_check_starts['WALLTIME']),
        np.average(restore_start['WALLTIME'] - failure_check_stops['WALLTIME']),
        np.average(restore_stop['WALLTIME'] - restore_start['WALLTIME'])
    )], columns=labels)

    fig, ax = plt.subplots(figsize=(4, 4))
    ax.bar(x=np.arange(5), height=plotData.loc[0].values)

    from mpl_toolkits.axes_grid1.inset_locator import inset_axes
    from mpl_toolkits.axes_grid1.inset_locator import mark_inset

    axins = inset_axes(ax, 1, 1)
    axins.bar(np.arange(5), height=plotData.loc[0].values)

    axins.set_xticks(np.arange(5))
    axins.set_xticklabels(labels, rotation='vertical')

    axins.set_xlim(1.5, 4.5)
    axins.set_ylim(0, 0.05)

    ax.set_xticks(np.arange(5))
    ax.set_xticklabels(['Propagate\nerror','Wait for\ndetection', 'Detect', 'React', 'Restore'], rotation='vertical')


    mark_inset(ax, axins, loc1=3, loc2=4)
#    plt.xscale('log')



    ax.set_title('Distribution of Activities\nduring the Recovery Process')
    ax.set_xlabel('Activity')
    ax.set_ylabel('Time (s)')
    plt.show()

    ax.get_figure().savefig('graphs/recovery-time-distribution.pdf', format='pdf')
    plotData.set_index(pd.Series(['Time (s)'])).transpose().to_csv('graphs/recovery-time-distribution-stats.csv',
                                                                   index_label='Activities')

TEST_MAX=10
# draw_runtime_boxplot()
# draw_restart_boxplot()
# draw_restart_boxplot(file_pattern="experiment_restart_time_mca_mpi_{0}_{1}_trace.csv",
#                      csv='graphs/restart-time-mca-stats.csv',
#                      pdf='graphs/restart-time-mca.pdf')
# draw_runtime_boxplot(file_pattern='experiment_restart_time_to_solution_mpi_{0}_{1}_trace.csv',
#                      title='Measured Time to Solution Restart Strategy',
#                      csv='graphs/restart-time-to-solution-stats.csv',
#                      pdf='graphs/restart-time-to-solution.pdf',
#                      time_column='WALLTIME',
#                      scatter=True)
# TEST_MAX=3
# draw_runtime_boxplot(file_pattern='experiment_recovery_time_mpi_{0}_{1}_trace.csv',
#                      title='Time for Checkpointing\n(Checkpoint Strategy)',
#                      csv='graphs/checkpoint-checkpoint-time-stats.csv',
#                      pdf='graphs/checkpoint-checkpoint-time.pdf',
#                      time_column='WALLTIME',
#                      evaluation_function=calculate_checkpoint_time)
# draw_runtime_boxplot(file_pattern='experiment_recovery_time_mpi_{0}_{1}_trace.csv',
#                      title='Time for Restoration\n(Checkpoint Strategy)',
#                      csv='graphs/checkpoint-restore-time-stats.csv',
#                      pdf='graphs/checkpoint-restore-time.pdf',
#                      time_column='WALLTIME',
#                      evaluation_function=calculate_recovery_time)
# TEST_MAX=3
# draw_runtime_boxplot(file_pattern='experiment_checkpoint_time_to_solution_mpi_{0}_{1}_trace.csv',
#                      title='Measured Time to Solution\n(Checkpoint Strategy)',
#                      csv='graphs/checkpoint-time-to-solution-stats.csv',
#                      pdf='graphs/checkpoint-time-to-solution.pdf',
#                      time_column='WALLTIME')

# calculate_restore_barchart(load_experiment('experiment_recovery_time_mpi_jac2d_0_trace.csv'))

# TEST_MAX=3
# draw_scaling_runtime_boxplot(file_pattern='experiment_mpi_scale_jac2d_{0}_{1}_trace.csv',
#                      title='Strong Scaling Test (Jac2D Benchmark)',
#                      csv='graphs/scaling-jac2d-stats.csv',
#                      pdf='graphs/scaling-jac2d.pdf',
#                      pdf2='graphs/scaling-jac2d-log.pdf',
#                      num_processes=[6, 12, 24, 48, 96, 192, 384, 768],
#                      time_column='TIME',
#                      evaluation_function=calculate_runtime,
#                      include_log_graph=True)
#
# TEST_MAX=3
# draw_scaling_runtime_boxplot(file_pattern='experiment_mpi_scale_weak_jac2d_{0}_{1}_trace.csv',
#                      title='Weak Scaling Test (Jac2D Benchmark)',
#                      csv='graphs/scaling-weak-jac2d-stats.csv',
#                      pdf='graphs/scaling-weak-jac2d.pdf',
#                      pdf2='graphs/scaling-jac2d-log.pdf',
#                      time_column='TIME',
#                      evaluation_function=calculate_runtime,
#                      num_processes=[6, 12, 24, 48, 96, 192, 384, 768],
#                      suppress_expected=True,
#                      suppress_weak_expected=False)

draw_runtime_boxplot_2(
                     file_pattern="experiment_restart_time_to_solution_mpi_{0}_{1}_trace.csv",
                     file_pattern_2='experiment_checkpoint_time_to_solution_mpi_{0}_{1}_trace.csv',
                     title='Measured Time to Solution',
                     csv='graphs/checkpoint-time-to-solution-stats.csv',
                     pdf='graphs/checkpoint-time-to-solution.pdf',
                     time_column='WALLTIME',
                     scatter=True)

# draw_memory_plot_2()
# draw_memory_plot_3(file_pattern='demo_mem/mem{0}-late.csv', pdf='graphs/demo-mem-late.pdf')
# draw_network_plot()

# draw_jac2d_example()
# draw_jac2d_example_2()


def draw_distribution_cutoff():
    fig, axs = plt.subplots(1, 2, figsize=(6,2))

    x = np.linspace(0, 4)
    pdf = lambda x: np.exp(-x)
    cdf = lambda x: 1 - np.exp(-x)
    axs[0].plot(x, pdf(x))
    axs[0].plot([2, 2], [0, 1])
    axs[0].set_xlabel(r'$x$')
    axs[0].set_ylabel(r'PDF $f(x)$')
    axs[1].plot(x, cdf(x))
    axs[1].plot([0, 2, 2], [cdf(2), cdf(2), 0])
    axs[1].set_xlabel(r'$x$')
    axs[1].set_ylabel(r'CDF $F(x)$')
    axs[1].set_title(' ')

    for i in range(2):
        axs[i].set_xlim([0,4])
        axs[i].set_ylim([0,1])
        axs[i].axvspan(2, 4, facecolor='0.95')

    arrow=dict(arrowstyle='->')
    plt.annotate(r'$1/\alpha$',
                 (2, cdf(2)),
                 xytext=(50, 0),
                 textcoords='offset points',
                 horizontalalignment='center',
                 verticalalignment='center',
                 arrowprops=arrow)

    plt.suptitle('Windowing the exponential distribution')

    plt.show()
    fig.savefig('graphs/exp-window.pdf', format='pdf')

# draw_distribution_cutoff()

def determine_actual_failure_rates():
    TEST_MAX = 10
    for file_pattern in ['experiment_restart_time_to_solution_mpi_{0}_{1}_trace.csv', 'experiment_checkpoint_time_to_solution_mpi_{0}_{1}_trace.csv']:
        for benchmark in ['osu', 'jac2d', 'lulesh']:
            time = 0.0
            failures = 0.0
            for i in range(TEST_MAX):
                test_data = load_experiment(file_pattern.format(benchmark, i))
                time += calculate_runtime(test_data, 'WALLTIME')
                failures += count_failures(test_data)
            print('Failure rate for {}: {} ({} / {})'.format(benchmark, failures/time, failures, time))

# determine_actual_failure_rates()