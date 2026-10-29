CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -Iinclude $(shell pkg-config --cflags glib-2.0 gio-2.0 mm-glib alsa)
LDFLAGS ?= $(shell pkg-config --libs glib-2.0 gio-2.0 mm-glib alsa) -lpthread

SRCDIR = src
OBJDIR = obj
BINDIR = bin
TESTDIR = tests

SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
TARGET = $(BINDIR)/mm2astrisk

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR) $(OBJDIR):
	mkdir -p $@

test: $(OBJDIR)/config.o $(OBJDIR)/audiosocket.o | $(BINDIR)
	$(CC) $(CFLAGS) tests/test_config.c $(OBJDIR)/config.o -o $(BINDIR)/test_config $(LDFLAGS)
	$(CC) $(CFLAGS) tests/test_audiosocket.c $(OBJDIR)/audiosocket.o -o $(BINDIR)/test_audiosocket $(LDFLAGS)
	$(BINDIR)/test_config
	$(BINDIR)/test_audiosocket

clean:
	rm -rf $(OBJDIR) $(BINDIR)

install: $(TARGET)
	install -d /usr/local/bin
	install -m 755 $(TARGET) /usr/local/bin/mm2astrisk
	install -d /etc/mm2astrisk
	test -f /etc/mm2astrisk/config.ini || install -m 600 config.ini /etc/mm2astrisk/config.ini
	install -d /etc/systemd/system
	install -m 644 systemd/mm2astrisk.service /etc/systemd/system/mm2astrisk.service
	systemctl daemon-reload

.PHONY: all clean test install
