#!/bin/sh
cd "$(dirname "$(readlink -f "$0")")"

echo "Stopping service if running..."
systemctl --user -q stop  SteamControllerGyroDSU.service 2>/dev/null
systemctl --user -q disable SteamControllerGyroDSU.service 2>/dev/null

echo "Installing binaries..."
mkdir -p "$HOME/SteamControllerGyroDSU"
cp SteamControllerGyroDSU       "$HOME/SteamControllerGyroDSU/"
chmod +x "$HOME/SteamControllerGyroDSU/SteamControllerGyroDSU"

# Install the GUI config tool if it was included in this release.
if [ -f sc2gyrodsu-config ]; then
    cp sc2gyrodsu-config "$HOME/SteamControllerGyroDSU/"
    chmod +x "$HOME/SteamControllerGyroDSU/sc2gyrodsu-config"
fi

echo "Installing scripts..."
cp update.sh   "$HOME/SteamControllerGyroDSU/"
cp uninstall.sh "$HOME/SteamControllerGyroDSU/"
chmod +x "$HOME/SteamControllerGyroDSU/update.sh"
chmod +x "$HOME/SteamControllerGyroDSU/uninstall.sh"

echo "Installing service..."
mkdir -p "$HOME/.config/systemd/user"
cat > "$HOME/.config/systemd/user/SteamControllerGyroDSU.service" << EOF
[Unit]
Description=Steam Controller Gyro DSU Server
After=sockets.target
StartLimitIntervalSec=0

[Service]
Type=simple
Restart=always
RestartSec=1
ExecStart=$HOME/SteamControllerGyroDSU/SteamControllerGyroDSU --port 26761

[Install]
WantedBy=default.target
EOF

systemctl --user enable --now SteamControllerGyroDSU.service

echo "Installing desktop shortcuts..."

cat > "$HOME/Desktop/Update SteamControllerGyroDSU.desktop" << EOF
[Desktop Entry]
Name=Update SteamControllerGyroDSU
Exec=$HOME/SteamControllerGyroDSU/update.sh
Icon=system-software-update
Terminal=true
Type=Application
EOF
chmod +x "$HOME/Desktop/Update SteamControllerGyroDSU.desktop"

cat > "$HOME/Desktop/Uninstall SteamControllerGyroDSU.desktop" << EOF
[Desktop Entry]
Name=Uninstall SteamControllerGyroDSU
Exec=$HOME/SteamControllerGyroDSU/uninstall.sh
Icon=edit-delete
Terminal=true
Type=Application
EOF
chmod +x "$HOME/Desktop/Uninstall SteamControllerGyroDSU.desktop"

# Desktop shortcut for the GUI config tool (only if binary is present).
if [ -f "$HOME/SteamControllerGyroDSU/sc2gyrodsu-config" ]; then
    cat > "$HOME/Desktop/SteamControllerGyroDSU Config.desktop" << EOF
[Desktop Entry]
Name=SteamControllerGyroDSU Config
Exec=$HOME/SteamControllerGyroDSU/sc2gyrodsu-config
Icon=input-gamepad
Terminal=false
Type=Application
EOF
    chmod +x "$HOME/Desktop/SteamControllerGyroDSU Config.desktop"
fi

echo ""
echo "Done! SteamControllerGyroDSU is running on port 26761."
echo "Point your emulator at 127.0.0.1:26761"
echo ""
read -n 1 -s -r -p "Press any key to exit."
echo ""
