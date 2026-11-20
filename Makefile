CC ?= gcc
CFLAGS ?= -std=gnu11 -Wall -Wextra -O2 -g
CPPFLAGS += -DCRONQ_VERSION=\"$(shell cat VERSION)\"
INCLUDES = $(shell pkg-config --cflags gtk+-3.0 webkit2gtk-4.1 x11 xi xext xrandr xtst)
LIBS = $(shell pkg-config --libs gtk+-3.0 webkit2gtk-4.1 x11 xi xext xrandr xtst) -lm

CRONQ_DATA_DIR ?= $(if $(XDG_DATA_HOME),$(XDG_DATA_HOME),$(HOME)/.local/share)
CRONQ_CONFIG_DIR ?= $(if $(XDG_CONFIG_HOME),$(XDG_CONFIG_HOME),$(HOME)/.config)

TARGET = bin/cronq
SRCS = src/main.c src/window.c src/hotkey.c src/raise.c src/xutil.c src/input.c src/config.c src/chat_view.c src/theme.c src/killswitch.c src/capture_guard.c src/lifecycle.c
OBJS = $(SRCS:.c=.o)
HEADERS = shared/hover_focus_script.inc src/chatgpt_script.inc src/window.h src/hotkey.h src/raise.h src/xutil.h src/input.h src/config.h src/chat_view.h src/theme.h src/killswitch.h src/capture_guard.h src/lifecycle.h

ADVERSARIES = tests/stackcheck tests/adversary/adv1_ewmh tests/adversary/adv2_override tests/adversary/adv3_focuslock
TEST_BINS = $(ADVERSARIES) tests/colorwin

all: $(TARGET)

tests/linux_runtime_test: tests/linux_runtime_test.c $(filter-out src/main.o,$(OBJS))
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -Isrc $^ $(LIBS) -o $@


$(TARGET): $(OBJS) | bin
	$(CC) $(OBJS) $(LIBS) -o $(TARGET)

src/%.o: src/%.c $(HEADERS) VERSION
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -c $< -o $@

bin:
	mkdir -p bin

adversaries: $(ADVERSARIES)

tests/colorwin: tests/colorwin.c
	$(CC) $(CFLAGS) $< -lX11 -o $@

tests/stackcheck: tests/stackcheck.c
	$(CC) $(CFLAGS) $< -lX11 -o $@

tests/adversary/adv1_ewmh: tests/adversary/adv1_ewmh.c
	$(CC) $(CFLAGS) $< -lX11 -o $@

tests/adversary/adv2_override: tests/adversary/adv2_override.c
	$(CC) $(CFLAGS) $< -lX11 -o $@

tests/adversary/adv3_focuslock: tests/adversary/adv3_focuslock.c
	$(CC) $(CFLAGS) $< -lX11 -o $@

install: $(TARGET)

	mkdir -p $(HOME)/.local/bin
	mkdir -p $(CRONQ_CONFIG_DIR)/autostart
	mkdir -p $(CRONQ_DATA_DIR)/applications
	mkdir -p $(CRONQ_DATA_DIR)/icons/hicolor/scalable/apps
	mkdir -p $(CRONQ_CONFIG_DIR)/cronq
	mkdir -p $(CRONQ_DATA_DIR)/cronq
	install -m 755 $(TARGET) $(HOME)/.local/bin/cronq
	install -m 755 scripts/cronq-supervisor $(HOME)/.local/bin/cronq-supervisor
	install -m 755 scripts/cronq-update.py $(CRONQ_DATA_DIR)/cronq/update.py
	install -m 644 VERSION $(CRONQ_DATA_DIR)/cronq/VERSION
	ln -sf cronq-supervisor $(HOME)/.local/bin/cronq-superisor
	install -m 644 cronq.desktop $(CRONQ_CONFIG_DIR)/autostart/cronq.desktop
	install -m 644 cronq.desktop $(CRONQ_DATA_DIR)/applications/cronq.desktop
	install -m 644 assets/cronq.svg $(CRONQ_DATA_DIR)/icons/hicolor/scalable/apps/cronq.svg
	@which update-desktop-database >/dev/null 2>&1 && update-desktop-database $(CRONQ_DATA_DIR)/applications 2>/dev/null || true
	@which gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t $(CRONQ_DATA_DIR)/icons/hicolor 2>/dev/null || true
	@echo "Installed cronq to $(HOME)/.local/bin, desktop entry to applications and autostart, and application icon."

uninstall:
	@pkill -9 -f cronq-supervisor 2>/dev/null || true
	@pkill -9 -f cronq 2>/dev/null || true
	@pkill -9 -f phoneoverlay-supervisor 2>/dev/null || true
	@pkill -9 -f phoneoverlay 2>/dev/null || true
	rm -f $(HOME)/.local/bin/cronq
	rm -f $(HOME)/.local/bin/cronq-supervisor
	rm -f $(CRONQ_DATA_DIR)/cronq/update.py $(CRONQ_DATA_DIR)/cronq/VERSION
	rm -f $(HOME)/.local/bin/cronq-superisor
	rm -f $(HOME)/.local/bin/phoneoverlay
	rm -f $(HOME)/.local/bin/phoneoverlay-supervisor
	rm -f $(HOME)/.local/bin/phone-overlay
	rm -f $(CRONQ_CONFIG_DIR)/autostart/cronq.desktop
	rm -f $(CRONQ_CONFIG_DIR)/autostart/phoneoverlay.desktop
	rm -f $(CRONQ_DATA_DIR)/applications/cronq.desktop
	rm -f $(CRONQ_DATA_DIR)/applications/phoneoverlay.desktop
	rm -f $(CRONQ_DATA_DIR)/icons/hicolor/scalable/apps/cronq.svg
	@which update-desktop-database >/dev/null 2>&1 && update-desktop-database $(CRONQ_DATA_DIR)/applications 2>/dev/null || true
	@which gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t $(CRONQ_DATA_DIR)/icons/hicolor 2>/dev/null || true
	@echo "Uninstalled cronq binary, supervisor, desktop entries, and application icon."

clean:
	rm -rf bin $(OBJS) $(TEST_BINS) tests/linux_runtime_test

run: $(TARGET)
	./$(TARGET)

test: $(TARGET) tests/linux_runtime_test
	@bash tests/run_linux_tests.sh

# Historical phase targets use the same disposable-display acceptance suite.
test1 test2 test3 test4 test5 test6: test

.PHONY: all clean run test test1 test2 test3 test4 test5 test6 adversaries install uninstall
