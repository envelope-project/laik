import sys
import glob

def output(input, output):
    print(input, "->", output)
    input = open(input, "r")
    buffer = input.readlines()
    output = open(output, "w")

    output.write('TIME,MEM\n')
    for line in buffer:
        if "time=" in line:
            output.write(line.replace("time=", "").replace("\n", "") + ', ')
        elif "mem_heap_B=" in line:
            output.write(line.replace("mem_heap_B=", "").replace("\n", "") + "\n")
    input.close()
    output.close()

count = 0
for input in glob.glob('massif.out.*'):
    output(input, "laik_experiments/data/demo_mem/mem{0}{1}.csv".format(count, sys.argv[1]))
    count += 1
