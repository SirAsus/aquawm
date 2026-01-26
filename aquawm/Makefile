CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lX11 -lpthread
TARGET = aquawm
SRC = aquawm.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -d /usr/local/bin/
	install -m 755 $(TARGET) /usr/local/bin/
	install -d /usr/share/wallpapers/aquawm/
	cp -n blue.png /usr/share/wallpapers/aquawm/blue.png 2>/dev/null || true
	install -d /etc/xdg/aquawm/
	install -m 644 aquawm.conf /etc/xdg/aquawm/ 2>/dev/null || true

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	rm -rf /usr/share/wallpapers/aquawm 2>/dev/null || true

.PHONY: all clean install uninstall
