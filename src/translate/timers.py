import contextlib
import os
import sys
import time

import tools


class Timer:
    def __init__(self):
        self.start_time = time.time()
        self.start_clock = self._clock()

    def _clock(self):
        times = os.times()
        return times[0] + times[1]

    def __str__(self):
        return "[%.3fs CPU, %.3fs wall-clock, %d KB]" % (
            self._clock() - self.start_clock,
            time.time() - self.start_time,
            tools.get_peak_memory_in_kb())


@contextlib.contextmanager
def timing(text, block=False):
    timer = Timer()
    if block:
        print("%s..." % text)
    else:
        print("%s..." % text, end=' ')
    sys.stdout.flush()
    yield
    if block:
        print("%s: %s" % (text, timer))
    else:
        print(timer)
    sys.stdout.flush()
