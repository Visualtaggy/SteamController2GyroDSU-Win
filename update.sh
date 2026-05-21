#!/bin/sh

echo "Grabbing latest release..."
if curl -L -O -s https://github.com/dylangmarinus-stack/Steam-Controller-2-GyroDSU/releases/latest/download/SteamControllerGyroDSUSetup.zip >/dev/null; then
    echo "Latest release downloaded."
else
    echo -e "\e[1mFailed to grab latest .zip file...\e[0m"
    exit 10
fi
echo "Extracting files..."
if unzip -o SteamControllerGyroDSUSetup.zip -d $HOME >/dev/null; then
    echo "Files extracted."
else
    echo -e "\e[1mFailed to extract files from downloaded .zip release...\e[0m"
    exit 11
fi

rm -f SteamControllerGyroDSUSetup.zip

cd $HOME/SteamControllerGyroDSUSetup
echo "Running install script..."
echo " "
./install.sh
code=$?

cd
rm -rf $HOME/SteamControllerGyroDSUSetup

read -n 1 -s -r -p "Finished. Press any key to exit."
echo " "

exit $code
