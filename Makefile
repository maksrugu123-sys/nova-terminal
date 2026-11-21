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
PARSER_TEST := parser_test
SOURCES := src/main.c src/editor.c src/shell_features.c src/process_manager.c src/parser.c

.PHONY: all test clean install uninstall
all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@

$(PARSER_TEST): tests/parser_test.c src/parser.c src/parser.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/parser_test.c src/parser.c $(LDFLAGS) -o $@

test: $(TARGET) $(PARSER_TEST)
	./$(PARSER_TEST)
	sh tests/smoke.sh

install: $(TARGET)
	mkdir -p "$(DESTDIR)$(BINDIR)"
	install -m 755 $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"

clean:
	rm -f $(TARGET) $(PARSER_TEST) *.o
