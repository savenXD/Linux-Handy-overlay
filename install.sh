#!/usr/bin/env bash
# ==============================================================================
# Cronq — Automated Consumer Installer
# Always-on-top, non-focus-stealing floating companion for Linux
# ==============================================================================
set -e

CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BOLD='\033[1m'
NC='\033[0m' # No Color

echo -e "${CYAN}${BOLD}"
echo "  ================================================================"
echo "    📱 Cronq — Floating Desktop Companion for Linux"
echo "  ================================================================"
echo -e "${NC}"

# 1. Platform Check
if [ "$(uname -s)" != "Linux" ]; then
    echo -e "${RED}Error: Cronq only supports Linux at this time.${NC}"
    exit 1
fi

# Detect display session
if [ -n "$XDG_SESSION_TYPE" ] && [ "$XDG_SESSION_TYPE" = "wayland" ]; then
    echo -e "${YELLOW}Notice: You are currently running on Wayland.${NC}"
    echo -e "${YELLOW}Cronq runs via XWayland / X11. For the best experience (full click-through${NC}"
    echo -e "${YELLOW}and global hotkeys), ensure XWayland is enabled or select Xorg on login.${NC}\n"
fi

# 2. Dependency Resolution
echo -e "${BOLD}[1/4] Checking and installing dependencies...${NC}"

has_pkg() {
    pkg-config --exists "$1" 2>/dev/null
}

NEED_DEPS=0
if ! command -v gcc >/dev/null 2>&1 || \
   ! command -v make >/dev/null 2>&1 || \
   ! command -v pkg-config >/dev/null 2>&1 || \
   ! has_pkg gtk+-3.0 || \
   ! has_pkg webkit2gtk-4.1 || \
   ! has_pkg x11 || \
   ! has_pkg xi || \
   ! has_pkg xtst || \
   ! has_pkg xrandr || \
   ! has_pkg xext || \
   ! command -v xdotool >/dev/null 2>&1 || \
   ! command -v xclip >/dev/null 2>&1; then
    NEED_DEPS=1
fi

if [ "$NEED_DEPS" -eq 1 ]; then
    echo -e "${YELLOW}Missing required build or runtime libraries. Installing now (sudo access required)...${NC}"
    if command -v apt-get >/dev/null 2>&1; then
        echo -e "${CYAN}Detected Debian / Ubuntu / Linux Mint (apt)...${NC}"
        sudo apt-get update
        sudo apt-get install -y build-essential pkg-config libgtk-3-dev \
            libwebkit2gtk-4.1-dev libx11-dev libxi-dev libxtst-dev \
            libxrandr-dev libxext-dev xdotool xclip maim python3 || \
        sudo apt-get install -y build-essential pkg-config libgtk-3-dev \
            libwebkit2gtk-4.0-dev libx11-dev libxi-dev libxtst-dev \
            libxrandr-dev libxext-dev xdotool xclip maim python3
    elif command -v dnf >/dev/null 2>&1; then
        echo -e "${CYAN}Detected Fedora / RHEL (dnf)...${NC}"
        sudo dnf install -y gcc gcc-c++ make pkgconf-pkg-config \
            gtk3-devel webkit2gtk4.1-devel libX11-devel libXi-devel \
            libXtst-devel libXrandr-devel libXext-devel xdotool xclip maim python3
    elif command -v pacman >/dev/null 2>&1; then
        echo -e "${CYAN}Detected Arch Linux / Manjaro (pacman)...${NC}"
        sudo pacman -S --needed --noconfirm base-devel gtk3 webkit2gtk-4.1 \
            libx11 libxi libxtst libxrandr libxext xdotool xclip maim python3
    elif command -v zypper >/dev/null 2>&1; then
        echo -e "${CYAN}Detected openSUSE (zypper)...${NC}"
        sudo zypper install -y gcc gcc-c++ make pkg-config gtk3-devel \
            webkit2gtk3-devel libX11-devel libXi-devel libXtst-devel \
            libXrandr-devel libXext-devel xdotool xclip maim python3
    else
        echo -e "${RED}Error: Unsupported package manager. Please install GTK3, WebKitGTK 4.1, and X11 development libraries manually.${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}✓ All build and runtime dependencies are present.${NC}"
fi

# 3. Compile Project
echo -e "\n${BOLD}[2/4] Compiling Cronq...${NC}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

make clean
make -j"$(nproc 2>/dev/null || echo 2)"
echo -e "${GREEN}✓ Build succeeded.${NC}"

# 4. Install Files
echo -e "\n${BOLD}[3/4] Installing binaries and desktop integration...${NC}"
make install

# Check PATH
BIN_DIR="$HOME/.local/bin"
case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *)
        echo -e "${YELLOW}Adding $BIN_DIR to your shell PATH...${NC}"
        if [ -n "$BASH_VERSION" ] && [ -f "$HOME/.bashrc" ]; then
            echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc"
        elif [ -n "$ZSH_VERSION" ] && [ -f "$HOME/.zshrc" ]; then
            echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.zshrc"
        elif [ -f "$HOME/.profile" ]; then
            echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.profile"
        fi
        export PATH="$BIN_DIR:$PATH"
        ;;
esac

# 5. Launch Background Service
echo -e "\n${BOLD}[4/4] Starting Cronq background daemon...${NC}"
# Terminate legacy phoneoverlay processes and remove obsolete autostart
pkill -9 -f phoneoverlay-supervisor 2>/dev/null || true
pkill -9 -f phoneoverlay 2>/dev/null || true
rm -f "$HOME/.config/autostart/phoneoverlay.desktop"
rm -f "$BIN_DIR/phoneoverlay" "$BIN_DIR/phoneoverlay-supervisor" "$BIN_DIR/phone-overlay"

pkill -9 -f cronq-supervisor 2>/dev/null || true
pkill -9 -f cronq 2>/dev/null || true
sleep 1

setsid nohup "$BIN_DIR/cronq-supervisor" </dev/null >/tmp/cronq-supervisor.log 2>&1 &
sleep 1

echo -e "\n${GREEN}${BOLD}================================================================${NC}"
echo -e "${GREEN}${BOLD}  🎉 Cronq is successfully installed and running!${NC}"
echo -e "${GREEN}${BOLD}================================================================${NC}"
echo -e "  • ${BOLD}Toggle Overlay${NC} : Press ${CYAN}[Alt] + [X]${NC} (or run: ${CYAN}cronq${NC})"
echo -e "  • ${BOLD}Move Window${NC}    : Click and drag the center handle capsule"
echo -e "  • ${BOLD}Ghost Mode${NC}     : Press ${CYAN}[Alt] + [G]${NC} to make it click-through"
echo -e "  • ${BOLD}Ghost Writer${NC}   : Click the Ghost button on the bottom nav to reveal input"
echo -e "  • ${BOLD}Reset / Refresh${NC}: Click the reload arrow on the bottom nav"
echo -e "  • ${BOLD}Stop Service${NC}   : Run ${CYAN}cronq kill${NC}"
echo -e "  • ${BOLD}Uninstall${NC}      : Run ${CYAN}./uninstall.sh${NC}"
echo -e "${GREEN}================================================================${NC}\n"
