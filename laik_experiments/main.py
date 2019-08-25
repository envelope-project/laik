import glob
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt


data = pd.read_csv("data/experiment_mpi_strong_scale_04_trace.csv")
ranks = len(data['RANK'].unique())

def drawTimeline():
    fig, ax = plt.subplots()
    eventColorMap = {
        'INIT': 'blue',
        'CHECKPOINT': 'green',
        'FAILURE-CHECK': 'orange',
        'RESTORE': 'red',
        'FINALIZE': 'blue',

        'COMM-ERROR': 'orange',
        'FAILURE-DETECT': 'black',
        'FAILURE-GENERATE': 'red',
        'ITER': 'gray',

        'INIT-START': 'black',
        'INIT-STOP': 'black',
        'CHECKPOINT-START': 'black',
        'CHECKPOINT-STOP': 'black',
        'FAILURE-CHECK-START': 'black',
        'FAILURE-CHECK-STOP': 'black',
        'RESTORE-START': 'black',
        'RESTORE-STOP': 'black',
        'FINALIZE-START': 'black',
        'FINALIZE-STOP': 'black',
    }
    eventMarkerMap = {
        'COMM-ERROR': 'x',
        'FAILURE-GENERATE': 'X',
        'FAILURE-DETECT': '',
        'ITER': '|',

        'INIT-START': '',
        'INIT-STOP': '',
        'CHECKPOINT-START': '',
        'CHECKPOINT-STOP': '',
        'FAILURE-CHECK-START': '',
        'FAILURE-CHECK-STOP': '',
        'RESTORE-START': '',
        'RESTORE-STOP': '',
        'FINALIZE-START': '',
        'FINALIZE-STOP': '',
    }
    boxData = data[data['DURATION'] != 0.0]
    for rank in boxData['RANK'].unique():
        myRank = boxData[boxData['RANK'] == rank]
        myItems = myRank[['TIME', 'DURATION']]
        myTypes = myRank['EVENT_TYPE'].replace(eventColorMap)
        tuples = [tuple(x) for x in myItems.values]
        ax.broken_barh(tuples, (rank - 0.25, 0.5), facecolors=myTypes)
    scatterData = data[data['DURATION'] == 0.0]
    for idx, point in scatterData.iterrows():
        ax.scatter(point['TIME'], point['RANK'],
                   marker=eventMarkerMap[point['EVENT_TYPE']],
                   color=eventColorMap[point['EVENT_TYPE']],
                   s=300)
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Rank')
    ax.set_yticks(range(ranks))
    ax.set_title("Timeline of Run")
    plt.show()


def linePlot(data, title, xlabel, ylabel, valueColumn, lineSeparator='RANK', legendLabel='Rank {}'):
    # TODO: THIS IS A LIE: SHOWING THE MEASURED VALUE FROM END OF DURATION AT START
    fig, ax = plt.subplots()
    unique = data[lineSeparator].unique()
    for rank in unique:
        myRank = data[(data[lineSeparator] == rank) & (data['DURATION'] == 0.0)]
        #myRank = myRank[]
        ax.plot(myRank['TIME'], myRank[valueColumn])
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.legend([legendLabel.format(str(x)) for x in unique])
    plt.show()

drawTimeline()
linePlot(data, 'Network Usage', 'Data transferred (KB)', 'Network usage (KB)', 'NET')
linePlot(data, 'Memory Usage', 'Allocated memory (KB)', 'Memory usage (KB)', 'MEM')


def loadMultipleDataFrames(fileNames, perFileMapper = None):
    data = pd.read_csv(fileNames[0])
    data = pd.DataFrame(columns=data.columns)

    data['FILE'] = ''
    data['FILE_ID'] = -1
    data['TEST_NAME'] = ''

    for fileId in range(len(fileNames)):
        filename = fileNames[fileId]
        fileData = pd.read_csv(filename)
        fileData['FILE'] = filename
        fileData['TEST_NAME'] = fileData['FILE'].replace(
            {'data/':'', 'experiment_':'', '_trace.csv':'', '_header.csv':''},
            regex=True
        )
        fileData['FILE_ID'] = fileId

        if perFileMapper is not None:
            fileData = perFileMapper(fileData)

        data = data.append(fileData, ignore_index=True)

    return data


def maxNormalizeColumn(data, columnName):
    data[columnName] = data[columnName] / np.max(data[columnName])
    return data



def aggregate(data: pd.DataFrame, groupcolumns, aggregatecolumns, operation) -> pd.DataFrame:
    return data.groupby(groupcolumns, as_index=False)[aggregatecolumns].agg(operation)


def drawScalingPlots(traces, headers):
    # Output the averaged memory consumption over all ranks for multiple different tests (scaling) over time
    fileNames = glob.glob(traces)
    data = loadMultipleDataFrames(fileNames, lambda x: maxNormalizeColumn(x, 'TIME'))
    data = aggregate(data, ['TEST_NAME', 'EVENT_SEQ'], ['TIME', 'MEM', 'NET'], np.mean)
    data['DURATION'] = 0.0
    # data['RANK'] = data['RANK'] + 100 * data['FILE_ID']
    linePlot(data, 'Memory Usage', 'Time (fraction of total runtime)', 'Allocated memory (KB)', 'MEM',
             lineSeparator='TEST_NAME', legendLabel='{}')
    # Now scale this according to their data set size
    headerNames = glob.glob(headers)
    header = loadMultipleDataFrames(headerNames)
    data['MEM_SCALED'] = data['MEM']
    for idx, test in header.iterrows():
        testSelected = data[data['TEST_NAME'] == test['TEST_NAME']]
        data.loc[data['TEST_NAME'] == test['TEST_NAME'], 'MEM_SCALED'] = testSelected['MEM_SCALED'] / test['DATA_SIZE']
    linePlot(data, 'Memory Usage relative to Data Set Size', 'Time (fraction of total runtime)',
             'Allocated memory (as fraction of data set size)', 'MEM_SCALED', lineSeparator='TEST_NAME',
             legendLabel='{}')


drawScalingPlots('data/experiment_mpi_strong_scale_*_trace.csv', 'data/experiment_mpi_strong_scale_*_header.csv')
drawScalingPlots('data/experiment_mpi_weak_scale_*_trace.csv', 'data/experiment_mpi_weak_scale_*_header.csv')


def drawBarPlot(startEvent, stopEvent):
    # Calculate the per experiment lag between fault occurence and fault detection
    traces = 'data/experiment_mpi_strong_scale_*_trace.csv'
    data = loadMultipleDataFrames(glob.glob(traces), lambda x: maxNormalizeColumn(x, 'TIME'))
    failureGenerated = aggregate(data[data['EVENT_TYPE'] == startEvent], ['TEST_NAME'], ['TIME'], np.min)
    failureDetected = aggregate(data[data['EVENT_TYPE'] == stopEvent], ['TEST_NAME'], ['TIME'], np.max)
    merged = failureDetected.merge(failureGenerated, on='TEST_NAME', suffixes=('_DETECTED', '_GENERATED'))
    entries = merged['TIME_DETECTED'] - merged['TIME_GENERATED']
    fig, ax = plt.subplots()
    ax.bar(range(len(entries)), entries)
    ax.set_xlabel('Experiment ID')
    ax.set_ylabel('Time from failure to detection (s)')
    ax.set_title('Time from failure to detection')
    plt.show()


drawBarPlot('FAILURE-GENERATE', 'FAILURE-DETECT')

def drawBarPlot2(startEvent, stopEvent):
    # Calculate the per experiment lag between fault occurence and fault detection
    traces = 'data/experiment_mpi_strong_scale_*_trace.csv'
    data = loadMultipleDataFrames(glob.glob(traces), lambda x: maxNormalizeColumn(x, 'TIME'))
    failureStart = aggregate(data[data['EVENT_TYPE'] == startEvent], ['TEST_NAME'], ['TIME'], np.min)
    failureStop = aggregate(data[data['EVENT_TYPE'] == stopEvent], ['TEST_NAME'], ['TIME'], np.max)
    merged = failureStart.merge(failureStop, on='TEST_NAME', suffixes=('_START', '_STOP'))
    entries = merged['TIME_STOP'] - merged['TIME_START']
    fig, ax = plt.subplots()
    ax.bar(range(len(entries)), entries)
    ax.set_xlabel('Experiment ID')
    ax.set_ylabel('Time from detection to resumption (s)')
    ax.set_title('Time from detection to resumption')
    plt.show()

drawBarPlot2('FAILURE-DETECT', 'RESTORE-STOP')
