import numpy as np

l=0.015
r=6.5
t=60
f=0.088
c=0.08

z = (1/f) + c

t_r = (np.exp(l * t) - 1) * r
print('Restore time: ', t_r)

result = 1*((1 - np.exp(-l * t)) / (l * np.exp(-l * t)) + t_r)
print(result)

r = 4.25

result = (t*f)*((1 - np.exp(-l * z)) / (l * np.exp(-l * z)) + (np.exp(l * z) - 1) * r)
print(result)


