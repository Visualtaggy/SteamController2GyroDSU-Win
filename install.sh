#!/bin/sh

cd "$(dirname "$(readlink -f "$0")")"

echo "Stopping the service if it's running..."
systemctl --user -q stop SteamControllerGyroDSU.service >/dev/null 2>&1
systemctl --user -q disable SteamControllerGyroDSU.service >/dev/null 2>&1

echo "Copying binary..."
if mkdir -p $HOME/SteamControllerGyroDSU >/dev/null; then
    :
else
    echo -e "\e[1mFailed to create a directory.\e[0m"
    exit 24
fi

rm $HOME/SteamControllerGyroDSU/SteamControllerGyroDSU >/dev/null 2>&1
if cp SteamControllerGyroDSU $HOME/SteamControllerGyroDSU/ >/dev/null; then
    :
else
    echo -e "\e[1mFailed to copy binary file.\e[0m"
    exit 25
fi
if chmod +x $HOME/SteamControllerGyroDSU/SteamControllerGyroDSU >/dev/null; then
    echo "Binary copied."
else
    echo -e "\e[1mFailed to set binary as executable.\e[0m"
    exit 26
fi

rm $HOME/SteamControllerGyroDSU/update.sh >/dev/null 2>&1
cp update.sh $HOME/SteamControllerGyroDSU/ >/dev/null
if chmod +x $HOME/SteamControllerGyroDSU/update.sh >/dev/null; then
    echo "Update script copied."
fi

rm $HOME/SteamControllerGyroDSU/uninstall.sh >/dev/null 2>&1
cp uninstall.sh $HOME/SteamControllerGyroDSU/ >/dev/null
if chmod +x $HOME/SteamControllerGyroDSU/uninstall.sh >/dev/null; then
    echo "Uninstall script copied."
fi

echo "Installing service..."
rm $HOME/.config/systemd/user/SteamControllerGyroDSU.service >/dev/null 2>&1
if cp SteamControllerGyroDSU.service $HOME/.config/systemd/user/; then
    :
else
    echo -e "\e[1mFailed to copy service file.\e[0m"
    exit 27
fi

if systemctl --user -q enable --now SteamControllerGyroDSU.service >/dev/null; then
    echo "Installation done."
else
    echo -e "\e[1mFailed enabling the service.\e[0m"
    exit 28
fi

echo "Setting up desktop shortcuts..."
cp update-steamcontrollergyrodsu.desktop $HOME/Desktop/ >/dev/null
if chmod +x $HOME/Desktop/update-steamcontrollergyrodsu.desktop >/dev/null; then
    echo "Update shortcut copied."
fi
cp uninstall-steamcontrollergyrodsu.desktop $HOME/Desktop/ >/dev/null
if chmod +x $HOME/Desktop/uninstall-steamcontrollergyrodsu.desktop >/dev/null; then
    echo "Uninstall shortcut copied."
fi

read -n 1 -s -r -p "Finished. Press any key to exit."
echo " "
