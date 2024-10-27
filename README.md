led-indicator
===

This is a simple LED indicator for Raspberry Pi. It uses the GPIO pins to control the LED.

## Pre-requisites

- gcc 12 or above
- GNU make
- libgpiod (```apt-get install libgpiod-dev```)
- sdbus-cpp (```apt-get install libsdbus-c++-dev```)

## Build and Install

```sh
make
make install

led-indicator policyfile > /etc/dbus-1/system.d/led-indicator.conf
led-indicator unitfile > /etc/systemd/system/led-indicator.service

# if you want to use GPIO other than 13(default), you can specify it as an argument:
# led-indicator unitfile --line=5 > /etc/systemd/system/led-indicator.service

systemctl daemon-reload
systemctl enable led-indicator
systemctl start led-indicator
```

## Usage

```sh
led-indicator set on
led-indicator set off
led-indicator set blink

led-indicator get
```

## Author

[Tomoatsu Shimada](https://www.shimarin.com) / [Walbrix Corporation](https://www.walbrix.co.jp)
