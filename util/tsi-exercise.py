#!/usr/bin/env python3
import argparse
import gpiozero
import math
import time

parser = argparse.ArgumentParser(description='Exercise the touch screen interface')
parser.add_argument('--period', type=int, default=5000, help='Clock period (us); default=5000')
parser.add_argument('--timeout', type=int, default=3000000, help='Timeout period (us); default=3000000')
parser.add_argument('--propagation', type=int, default=100, help='Propagation delay (us); default=100')
parser.add_argument('--invert', type=bool, default=True, help='Treat incoming signals as inverted; default=true')
parser.add_argument('--clockpin', type=int, default=26, help='Clock GPIO pin; default=26')
parser.add_argument('--datapin', type=int, default=27, help='Data GPIO pin; default=27')
parser.add_argument('--runs', type=int, default=None, help='Times to fetch position; default=infinite')
parser.add_argument('--clockonly', default=False, const=True, action='store_const', help='Only send clock signal')
args = parser.parse_args()

clk = gpiozero.LED(args.clockpin)
data = gpiozero.Button(args.datapin, pull_up=None, active_state=not args.invert)
US_TO_S = 1000000.0

def tick():
    time.sleep(args.period / US_TO_S)
    clk.toggle()
    time.sleep(args.propagation / US_TO_S)

class Exerciser:
    def __init__(self, args):
        self.__args = args
        self.__ran = False
        if args.clockonly:
            self.__state = self._clockonly
        else:
            self.__state = self._start

    def _start(self):
        self.__state = self._query

    def _query(self):
        attempts = math.ceil(self.__args.timeout / self.__args.period)
        for i in range(attempts):
            val = self._recv('.')
            if val == 1:
                print()
                self.__state = self._acquire
                return

        self.__state = self._timeout

    def _acquire(self):
        print('Receiving data: ')

        (x, csx) = self._recv_number()
        (y, csy) = self._recv_number()

        expected_checksum = csx ^ csy
        actual_checksum = self._recv()
        finish = self._recv()

        if expected_checksum != actual_checksum:
            self.__state = self._bad_checksum
            return
        if finish != 0:
            self.__state = self._finish
            return

        print()
        print('x =', x, 'y =', y)
        self.__state = self._end

    def _recv(self, writeout = None):
        tick()
        val = data.value
        print(writeout if writeout else val, end='')
        return val

    def _recv_number(self):
        num = 0
        checksum = 0
        for i in range(10):
            val = self._recv()
            num = (num << 1) | val
            checksum ^= val
        return (num, checksum)

    def _timeout(self): self._err('Timed out waiting for a signal.')
    def _bad_checksum(self): self._err('Incorrect checksum.')
    def _finish(self): self._err('Incomplete message.')
    def _err(self, msg):
        print()
        print('ERROR!', msg, 'Waiting for peripheral to time out')
        time.sleep(self.__args.timeout / US_TO_S)
        self.__state = self._end

    def _end(self):
        self.__ran = True
        self.__state = self._start

    def _clockonly(self):
        self._recv('.')
        self.__ran = True

    def run(self):
        runs_elapsed = 1
        runs_expected = self.__args.runs
        while True:
            self.__state()
            if runs_expected and self.__ran:
                if runs_expected == runs_elapsed: return
                runs_elapsed += 1
                self.__ran = False

Exerciser(args).run()
print()
