#!/bin/sh

echo "Uninstalling the service"
systemctl --user -q stop SteamControllerGyroDSU.service >/dev/null 2>&1
systemctl --user -q disable SteamControllerGyroDSU.service >/dev/null 2>&1
rm $HOME/.config/systemd/user/SteamControllerGyroDSU.service >/dev/null 2>&1

echo "Removing files"
rm $HOME/SteamControllerGyroDSU/SteamControllerGyroDSU >/dev/null 2>&1
rm $HOME/SteamControllerGyroDSU/update.sh >/dev/null 2>&1
rm $HOME/SteamControllerGyroDSU/uninstall.sh >/dev/null 2>&1
rm -d $HOME/SteamControllerGyroDSU >/dev/null 2>&1
rm $HOME/Desktop/update-steamcontrollergyrodsu.desktop >/dev/null 2>&1
rm $HOME/Desktop/uninstall-steamcontrollergyrodsu.desktop >/dev/null 2>&1

echo "Uninstalling complete."

read -n 1 -s -r -p "Finished. Press any key to exit."
echo " "
