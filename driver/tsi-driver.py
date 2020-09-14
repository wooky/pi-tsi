#!/usr/bin/env python3
from argparse import Namespace
from logging import Logger

def us_to_s(param: float) -> float:
    return param / 1000000.0

class Clk:
    from gpiozero import LED
    import time

    def __init__(self, args: Namespace) -> None:
        self.__pin = LED(args.clockpin)
        self.__period = us_to_s(args.period)
        self.__propagation = us_to_s(args.propagation)

    def tick(self) -> None:
        time.sleep(self.__period)
        self.__pin.toggle()
        time.sleep(self.__propagation)

    def high(self) -> None:
        self.__pin.on()

class Data:
    from gpiozero import Button

    def __init__(self, args: Namespace) -> None:
        self.__pin = Button(args.datapin, pull_up=None, active_state=not args.invert)

    def recv(self) -> int:
        return self.__pin.value

    def wait(self) -> None:
        self.__pin.wait_for_press()

class DataPacket:
    """Packet in the form XXXXXXXXXXYYYYYYYYYYC0"""
    """                21-^                  0-^"""

    def __init__(self, logger: Logger, clk: Clk, data: Data) -> None:
        self.__logger = logger
        self.__clk = clk
        self.__data = data
        self.__packet = 0
        self.__checksum = 0

    def recv_all(self) -> None:
        self.__logger.debug("Receiving packet")
        for i in range(22):
            self.__clk.tick()
            value = self.__data.recv()
            self.__packet = (self.__packet << 1) | value
            self.__checksum ^= value
        self.__logger.debug("Received packet {:022b}", self.__packet)

    def is_checksum_bad(self) -> bool:
        return self.__checksum == 1

    def is_unfinished(self) -> bool:
        return (self.__packet & 0x1) == 1

    def get_x(self) -> int:
        return self.__packet >> 12

    def get_y(self) -> int:
        return (self.__packet >> 2) & 0x400

class Mouse:
    from pynput.mouse import Button, Controller

    def __init__(self, logger: Logger) -> None:
        self.__logger = logger
        self.__controller = Controller()
        self.__pressed = False

    def update(self, x: int, y: int) -> None:
        if x == 0 and y == 0:
            if self.__pressed:
                self.__pressed = False
                self.__controller.release(Button.left)
                self.__logger.debug("Mouse released")
        else:
            self.__controller.position = (x, y)
            self.__logger.debug("Mouse moved to ({},{})", x, y)
            if not self.__pressed:
                self.__pressed = True
                self.__controller.press(Button.left)
                self.__logger.debug("Mouse pressed")

class Driver:
    import time

    def __init__(self, logger: Logger, args: Namespace) -> None:
        self.__logger = logger
        self.__clk = Clk(args)
        self.__data = Data(args)
        self.__mouse = Mouse(logger)
        self.__timeout = us_to_s(args.timeout)

        logger.info("Driver initialized")

    def accept(self) -> None:
        self.__clk.high()
        self.__clk.wait()
        data_packet = DataPacket(self.__logger, self.__clk, self.__data)
        data_packet.recv_all()
        if data_packet.is_checksum_bad():
            self.__error("Bad checksum")
        elif data_packet.is_unfinished():
            self.__error("Packet was not finished")
        else:
            self.__mouse.update(data_packet.get_x(), data_packet.get_y())

    def __error(self, msg: str) -> None:
        self.__logger.error("Failed to get touch position, timing out: {}", msg)
        time.sleep(self.__timeout)

if __name__ == "__main__":
    from argparse import ArgumentParser
    import logging, logging.handlers

    parser = ArgumentParser(description='Touch screen interface driver')
    parser.add_argument('--period', type=int, default=5000, help='Clock period (us); default=5000')
    parser.add_argument('--timeout', type=int, default=3000000, help='Timeout period (us); default=3000000')
    parser.add_argument('--propagation', type=int, default=100, help='Propagation delay (us); default=100')
    parser.add_argument('--invert', type=bool, default=True, help='Treat incoming signals as inverted; default=true')
    parser.add_argument('--clockpin', type=int, default=26, help='Clock GPIO pin; default=26')
    parser.add_argument('--datapin', type=int, default=27, help='Data GPIO pin; default=27')
    parser.add_argument('--width', type=int, default=640, help='Screen width; default=640')
    parser.add_argument('--height', type=int, default=480, help='Screen height; default=480')
    parser.add_argument('--debug', default=False, const=True, action='store_const', help='Debug mode')
    args = parser.parse_args()

    logger = logging.getLogger('tsi-driver')
    if args.debug:
        logger.addHandler(logging.StreamHandler())
        logger.setLevel(logging.DEBUG)
    else:
        logger.addHandler(logging.handlers.SysLogHandler())
        logger.setLevel(logging.INFO)

    try:
        driver = Driver(logger, args)
        while True:
            driver.accept()
    except BaseException as e:
        logger.exception("Driver is killed", exc_info=e)
