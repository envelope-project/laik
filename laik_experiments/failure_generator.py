import random

def restart_failure_generator():
    num_iter = 1320
    failure_rate = 0.000979644
    count = 0

    for i in range(10):
        print(random.randint(0, num_iter), end=' ')
    print('')

    for success in range(10):
        for i in range(num_iter):
            if random.random() <= failure_rate:
                print(i, end=' ')
                i = 0
                count+=1
        print(-1, end=' ')
        count+=1

    print('')

    for rank in range(100):
        print(random.randint(0, 27), end=' ')

    print('')


restart_failure_generator()