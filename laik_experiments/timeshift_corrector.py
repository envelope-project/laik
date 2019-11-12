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
