# PiNfoScreen
Firmware, driver, and server for custom touchscreen.

## Driver Specs
![Specs](http://www.plantuml.com/plantuml/proxy?src=https://raw.githubusercontent.com/wooky/pinfoscreen/master/driver.puml)

TL;DR:
* MCU starts polling for coordinates only when IFACE_DATA = 1
* When the coordinates are not (0,0), it sends the polled coordinates to the Pi
* When the coordinates are (0,0), but the last polled coordinates were not (0,0), it sends that to the Pi
* Otherwise, it keeps IFACE_DATA low
* If it detects weird signals from the Pi, it will keep acting as if there are no coordinates
* If the Pi detects weird signals from the MCU, it will time out to let the MCU know that there's an issue
