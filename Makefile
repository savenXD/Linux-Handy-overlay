CC ?= gcc
CFLAGS ?= -std=gnu11 -Wall -Wextra -O2 -g
INCLUDES = $(shell pkg-config --cflags gtk+-3.0 webkit2gtk-4.1 x11 xi xext xrandr xtst)
LIBS = $(shell pkg-config --libs gtk+-3.0 webkit2gtk-4.1 x11 xi xext xrandr xtst) -lm

TARGET = bin/cronq
SRCS = src/main.c src/window.c src/hotkey.c src/raise.c src/xutil.c src/input.c src/config.c src/browser.c src/ghost.c src/theme.c src/killswitch.c src/capture_guard.c
OBJS = $(SRCS:.c=.o)
HEADERS = src/window.h src/hotkey.h src/raise.h src/xutil.h src/input.h src/config.h src/browser.h src/ghost.h src/theme.h src/killswitch.h src/capture_guard.h

ADVERSARIES = tests/stackcheck tests/adversary/adv1_ewmh tests/adversary/adv2_override tests/adversary/adv3_focuslock
TEST_BINS = $(ADVERSARIES) tests/colorwin

all: $(TARGET)

$(TARGET): $(OBJS) | bin
	$(CC) $(OBJS) $(LIBS) -o $(TARGET)

src/%.o: src/%.c $(HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

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
	mkdir -p $(HOME)/.config/autostart
	mkdir -p $(HOME)/.local/share/applications
	mkdir -p $(HOME)/.local/share/icons/hicolor/scalable/apps
	mkdir -p $(HOME)/.config/cronq
	mkdir -p $(HOME)/.local/share/cronq
	install -m 755 $(TARGET) $(HOME)/.local/bin/cronq
	install -m 755 scripts/cronq-supervisor $(HOME)/.local/bin/cronq-supervisor
	ln -sf cronq-supervisor $(HOME)/.local/bin/cronq-superisor
	install -m 644 cronq.desktop $(HOME)/.config/autostart/cronq.desktop
	install -m 644 cronq.desktop $(HOME)/.local/share/applications/cronq.desktop
	install -m 644 assets/cronq.svg $(HOME)/.local/share/icons/hicolor/scalable/apps/cronq.svg
	@which update-desktop-database >/dev/null 2>&1 && update-desktop-database $(HOME)/.local/share/applications 2>/dev/null || true
	@which gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t $(HOME)/.local/share/icons/hicolor 2>/dev/null || true
	@echo "Installed cronq to $(HOME)/.local/bin, desktop entry to applications and autostart, and application icon."

uninstall:
	@pkill -9 -f cronq-supervisor 2>/dev/null || true
	@pkill -9 -f cronq 2>/dev/null || true
	@pkill -9 -f phoneoverlay-supervisor 2>/dev/null || true
	@pkill -9 -f phoneoverlay 2>/dev/null || true
	rm -f $(HOME)/.local/bin/cronq
	rm -f $(HOME)/.local/bin/cronq-supervisor
	rm -f $(HOME)/.local/bin/cronq-superisor
	rm -f $(HOME)/.local/bin/phoneoverlay
	rm -f $(HOME)/.local/bin/phoneoverlay-supervisor
	rm -f $(HOME)/.local/bin/phone-overlay
	rm -f $(HOME)/.config/autostart/cronq.desktop
	rm -f $(HOME)/.config/autostart/phoneoverlay.desktop
	rm -f $(HOME)/.local/share/applications/cronq.desktop
	rm -f $(HOME)/.local/share/applications/phoneoverlay.desktop
	rm -f $(HOME)/.local/share/icons/hicolor/scalable/apps/cronq.svg
	@which update-desktop-database >/dev/null 2>&1 && update-desktop-database $(HOME)/.local/share/applications 2>/dev/null || true
	@which gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t $(HOME)/.local/share/icons/hicolor 2>/dev/null || true
	@echo "Uninstalled cronq binary, supervisor, desktop entries, and application icon."

clean:
	rm -rf bin $(OBJS) $(TEST_BINS) /tmp/cronq*.log /tmp/p3*.log /tmp/p4*.log /tmp/phoneoverlay*.log

run: $(TARGET)
	./$(TARGET)

test: $(TARGET) $(TEST_BINS)
	@bash tests/run_all_tests.sh

test1: $(TARGET)
	@bash tests/acceptance_phase1.sh

test2: $(TARGET) adversaries
	@bash tests/run_adversaries.sh

test3: $(TARGET) adversaries
	@bash tests/run_phase3_tests.sh

test4: $(TARGET)
	@bash tests/test_phase4_browser.sh

test5: $(TARGET) adversaries
	@bash tests/test_phase5_apps.sh

test6: $(TARGET) $(TEST_BINS)
	@bash tests/test_phase6_snap.sh

.PHONY: all clean run test test1 test2 test3 test4 test5 test6 adversaries install uninstall
