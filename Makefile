PREFIX ?= /usr/local

all: led-indicator

led-indicator: led-indicator.cpp
	g++ -std=c++23 -o $@ $< -lgpiodcxx -lgpiod -lsdbus-c++

clean:
	rm -f led-indicator

install:
	install -Dm755 led-indicator $(DESTDIR)$(PREFIX)/bin/led-indicator
