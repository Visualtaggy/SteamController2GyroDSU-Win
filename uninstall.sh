#!/bin/sh

echo "Stopping and disabling service..."
systemctl --user -q stop    SteamControllerGyroDSU.service 2>/dev/null
systemctl --user -q disable SteamControllerGyroDSU.service 2>/dev/null
rm -f "$HOME/.config/systemd/user/SteamControllerGyroDSU.service"

echo "Removing files..."
rm -f "$HOME/SteamControllerGyroDSU/SteamControllerGyroDSU"
rm -f "$HOME/SteamControllerGyroDSU/sc2gyrodsu-config"
rm -f "$HOME/SteamControllerGyroDSU/update.sh"
rm -f "$HOME/SteamControllerGyroDSU/uninstall.sh"
rmdir "$HOME/SteamControllerGyroDSU" 2>/dev/null

echo "Removing desktop shortcuts..."
rm -f "$HOME/Desktop/Update SteamControllerGyroDSU.desktop"
rm -f "$HOME/Desktop/Uninstall SteamControllerGyroDSU.desktop"
rm -f "$HOME/Desktop/SteamControllerGyroDSU Config.desktop"

echo ""
echo "Uninstall complete."
echo ""
read -n 1 -s -r -p "Press any key to exit."
echo ""
