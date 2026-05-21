#!/bin/sh
echo "Downloading latest SteamControllerGyroDSU..."
cd /tmp
curl -L -O https://github.com/dylangmarinus-stack/Steam-Controller-2-GyroDSU/releases/latest/download/SteamControllerGyroDSUSetup.zip
echo "Extracting..."
unzip -o SteamControllerGyroDSUSetup.zip -d /tmp
cd /tmp/SteamControllerGyroDSUSetup
echo "Installing..."
bash install.sh
rm -rf /tmp/SteamControllerGyroDSUSetup /tmp/SteamControllerGyroDSUSetup.zip
