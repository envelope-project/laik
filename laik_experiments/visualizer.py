import logging
import os
import os.path
import sys
from subprocess import Popen

import imageio
import matplotlib.animation as animation
import matplotlib.pyplot as plt
import numpy as np

format=logging.basicConfig(format='%(asctime)s %(levelname)-8s %(message)s',
    level=logging.DEBUG,
    datefmt='%Y-%m-%d %H:%M:%S')


# path="../output/data_live_0_0.ppm"
# file= path + "data_live_0_0.ppm"
file = "/home/pi/ga26poh/laik/output/data_live_0_0.ppm"

# plt.ion()
# fig, ax = plt.subplots()
# image = np.array(imageio.imread(file), dtype=np.uint8)
# imageHandle = ax.imshow(image)
# plt.show()
#
#
# class FSEventHandler(pyinotify.ProcessEvent):
#     def process_default(self, event):
#         """
#         Eventually, this method is called for all others types of events.
#         This method can be useful when an action fits all events.
#         """
#         logging.info("Received event")
#         try:
#             image = np.array(imageio.imread(file), dtype=np.uint8)
#             imageHandle.set_data(image)
#             plt.draw()
#         except ValueError:
#             print('Failed to read file')
#             return
#
# # Instanciate a new WatchManager (will be used to store watches).
# wm = pyinotify.WatchManager()
# # Associate this WatchManager with a Notifier (will be used to report and
# # process events).
# notifier = pyinotify.Notifier(wm, FSEventHandler())
# # Add a new watch on /tmp for ALL_EVENTS.
# wm.add_watch(path, pyinotify.IN_CLOSE_WRITE)
#
# logging.info("Ready to receive events")
#
# # Loop forever and handle events.
# notifier.loop()

fig = plt.figure()
dataSetSize = 32
data = np.array(np.zeros([dataSetSize, dataSetSize], dtype=np.uint8), dtype=np.uint8)
image = plt.imshow(data, animated=True)
modified = 0

numUpdates = 0
numIter = 0

subprocessArgs = sys.argv[1:]
process = None

def draw(*args):
    global modified, numUpdates, numIter, process

    try:
        if os.path.isfile(file):
        # if os.path.isfile(file):
            mtime = os.stat(file).st_mtime
            if mtime > modified:
                data = np.array(imageio.imread(file), dtype=np.uint8)
                image.set_data(data)
                modified = mtime
                # logging.info("Reloaded image")
                numUpdates += 1
            else:
                # logging.info("Unmodified")
                modified = mtime
        else:
            logging.info("Not exists")
            data = np.array(np.zeros([dataSetSize, dataSetSize]), dtype=np.uint8)
            image.set_data(data)

        # logging.info("Updated animation")
        numIter += 1
        if numIter > 10:
            logging.info("Posted %i updates in %i iterations.", numUpdates, numIter)
            numUpdates = 0
            numIter = 0

        if process is None:
            logging.info("Starting subprocess: %s", ' '.join(subprocessArgs))
            process = Popen(subprocessArgs, shell=False)
        # logging.info("Polling for subprocess status.")
        returnCode = process.poll()
        if returnCode is not None:
            print("Subprocess exit code: ", returnCode)
            sys.exit(returnCode)
    except RuntimeError as e:
        print("Runtime error: ", e)
    return image,

def onclick(event):
    print("Click event registered. Aborting application!")
    process.kill()
    sys.exit()

cid = fig.canvas.mpl_connect('button_press_event', onclick)

animation = animation.FuncAnimation(fig, draw, interval=1000, blit=True)

mng = plt.get_current_fig_manager()
mng.full_screen_toggle()

logging.info("Showing animation")
plt.show()
