CC = gcc
CFLAGS = -Wall -O2
TARGET = fij

# Add paths to project's headers
INCDIRS  ?= -I. -I../fij/include -I../fij/include/uapi

all: $(TARGET)

$(TARGET): fij.c
	$(CC) $(CFLAGS) $(INCDIRS) -o $(TARGET) fij.c

install: $(TARGET)
	install -m 0755 $(TARGET) /usr/local/bin/$(TARGET)

uninstall:
	rm -f /usr/local/bin/$(TARGET)

clean:
	rm -f $(TARGET)

