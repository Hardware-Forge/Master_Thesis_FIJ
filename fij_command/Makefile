CC = gcc
CFLAGS = -Wall -O2
TARGET = fij

all: $(TARGET)

$(TARGET): fij.c
	$(CC) $(CFLAGS) -o $(TARGET) fij.c

install: $(TARGET)
	install -m 0755 $(TARGET) /usr/local/bin/$(TARGET)

uninstall:
	rm -f /usr/local/bin/$(TARGET)

clean:
	rm -f $(TARGET)

