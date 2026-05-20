#!/bin/sh
cd "$(dirname "$(readlink -f "$0")")"

echo "Stopping service if running..."
systemctl --user -q stop sc2gyrodsu.service 2>/dev/null
systemctl --user -q disable sc2gyrodsu.service 2>/dev/null

echo "Copying binary..."
mkdir -p "$HOME/sc2gyrodsu"
if cp sc2gyrodsu "$HOME/sc2gyrodsu/sc2gyrodsu" && chmod +x "$HOME/sc2gyrodsu/sc2gyrodsu"; then
    echo "Binary installed."
else
    echo "ERROR: Failed to copy binary."
    exit 1
fi

echo "Installing service..."
mkdir -p "$HOME/.config/systemd/user"
cp sc2gyrodsu.service "$HOME/.config/systemd/user/"

if systemctl --user enable --now sc2gyrodsu.service; then
    echo "Service installed and started."
else
    echo "ERROR: Failed to enable service."
    exit 1
fi

echo ""
echo "Done! sc2gyrodsu is running."
echo "Point your emulator at 127.0.0.1:26760"
echo ""
echo "To check status: systemctl --user status sc2gyrodsu"
echo "To view logs:    journalctl --user -u sc2gyrodsu -f"
