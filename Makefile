CC ?= clang
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?=
LDFLAGS ?=
PREFIX ?= $(shell printf '%s' "$$PREFIX")
ifeq ($(strip $(PREFIX)),)
PREFIX := /usr/local
endif
BINDIR := $(PREFIX)/bin
TARGET := nova
SOURCES := src/main.c

.PHONY: all clean install uninstall
all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@

install: $(TARGET)
	mkdir -p "$(DESTDIR)$(BINDIR)"
	install -m 755 $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"

clean:
	rm -f $(TARGET) *.o
