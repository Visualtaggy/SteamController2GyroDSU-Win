#!/bin/sh
echo "Uninstalling SteamControllerGyroDSU..."
systemctl --user stop SteamControllerGyroDSU.service 2>/dev/null
systemctl --user disable SteamControllerGyroDSU.service 2>/dev/null
rm -f "$HOME/.config/systemd/user/SteamControllerGyroDSU.service"
rm -rf "$HOME/SteamControllerGyroDSU"
rm -f "$HOME/Desktop/Update SteamControllerGyroDSU.desktop"
rm -f "$HOME/Desktop/Uninstall SteamControllerGyroDSU.desktop"
echo "Uninstalled."
read -n 1 -s -r -p "Press any key to exit."
echo ""
