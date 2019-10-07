import numpy as np
import imageio
import matplotlib.pyplot as plt
import pyinotify


path="/home/vincent_bode/Desktop/VTStuff/GitSync/Projects/CPP/laik/output/"
file= path + "data_live_0_0.ppm"

plt.ion()
fig, ax = plt.subplots()
image = np.array(imageio.imread(file), dtype=np.uint8)
ax.imshow(image)
plt.show()


class FSEventHandler(pyinotify.ProcessEvent):
    def process_default(self, event):
        """
        Eventually, this method is called for all others types of events.
        This method can be useful when an action fits all events.
        """
        print("Received event")
        try:
            image = np.array(imageio.imread(file), dtype=np.uint8)
        except ValueError:
            print('Failed to read file')
            return
        ax.imshow(image)
        plt.show()


# Instanciate a new WatchManager (will be used to store watches).
wm = pyinotify.WatchManager()
# Associate this WatchManager with a Notifier (will be used to report and
# process events).
notifier = pyinotify.Notifier(wm, FSEventHandler())
# Add a new watch on /tmp for ALL_EVENTS.
wm.add_watch(path, pyinotify.ALL_EVENTS)
# Loop forever and handle events.
notifier.loop()
