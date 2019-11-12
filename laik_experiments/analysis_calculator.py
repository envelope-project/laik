import numpy as np

l=0.015
r=6.5
t=60
f=0.088
c=0.08


def calculate_z():
    global z
    z = (1 / f) + c


calculate_z()

t_r = (np.exp(l * t) - 1) * r
print('Restore time: ', t_r)

result = 1*((1 - np.exp(-l * t)) / (l * np.exp(-l * t)) + t_r)
print(result)

r = 4.25


def calc_checkpoint_restart():
    return (t * f) * (np.exp(l * ((1/ f) + c) ) - 1) * ((1/l) + r)


result = calc_checkpoint_restart()
print(result)

values = []
xValues = np.linspace(0.015, 3, 500)
for f_new in xValues:
    f = f_new
    calculate_z()
    values.append(calc_checkpoint_restart())

import matplotlib.pyplot as plt
fig, ax = plt.subplots(figsize=(6,3))
plt.plot(xValues, values)
ax.set_xlim(0, None)
ax.set_ylim(0, None)

minValue = min(values)
minXValue = xValues[values.index(minValue)]
plt.scatter(minXValue, minValue)

arrow = dict(arrowstyle='->')
plt.annotate('                       Minimum: ({:.2f}, {:.2f})'.format(minXValue, minValue),
             (minXValue, minValue),
             xytext=(0, -50),
             textcoords='offset points',
             horizontalalignment='center',
             verticalalignment='center',
             arrowprops=arrow)

plt.xlabel('Checkpoint Frequency (Hz)')
plt.ylabel('Expected Runtime (s)')
plt.title('Optimizing for Expected Runtime using Checkpoint Frequency')
plt.show()
fig.savefig('graphs/optimize-checkpoint.pdf', format='pdf')

