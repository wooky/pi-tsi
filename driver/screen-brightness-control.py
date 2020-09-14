if __name__ == "__main__":
    from argparse import ArgumentParser
    from gpiozero import LED
    import logging, logging.handlers
    import subprocess

    parser = ArgumentParser(description='Touch screen interface driver')
    parser.add_argument('--pin', type=int, default=25, help='Screen brightness GPIO pin; default=25')
    parser.add_argument('--debug', default=False, const=True, action='store_const', help='Debug mode')
    args = parser.parse_args()

    logger = logging.getLogger('screen-brightness-control')
    if args.debug:
        logger.addHandler(logging.StreamHandler())
        logger.setLevel(logging.DEBUG)
    else:
        logger.addHandler(logging.handlers.SysLogHandler())
        logger.setLevel(logging.INFO)

    try:
        pin = LED(args.pin)
        pin.on()

        with subprocess.Popen(['xscreensaver-command', '-watch'], bufsize=1, stdout=subprocess.PIPE, text=True) as monitor:
            for line in monitor.stdout:
                if line.startswith('BLANK '):
                    pin.off()
                elif line.startswith('UNBLANK '):
                    pin.on()
    except BaseException as e:
        logger.exception("Controller is killed", exc_info=e)
