# SteamControllerGyroDSU

DSU (Cemuhook protocol) server for motion data for the **Steam Controller 2** on SteamOS/Linux.

Inspired by [SteamDeckGyroDSU](https://github.com/kmicki/SteamDeckGyroDSU) by kmicki.

## Features
- Supports up to **4 Steam Controllers simultaneously** (slots 0-3)
- Works via **Bluetooth**, **USB-C**, and **Proteus Puck dongle**
- Works along side **SteamDeckGyroDSU**
- Automatic gyro bias calibration
- Runs as a background service — starts automatically with your Steam Deck
- Compatible with **Eden**, **Ryujinx**, **Cemu**, and any other Cemuhook-compatible emulator

## Install

Open this page in the browser on your **Steam Deck** in Desktop Mode.

Download [SteamControllerGyroDSU](https://github.com/dylangmarinus-stack/Steam-Controller-GyroDSU/releases/latest/download/SteamControllerGyroDSU.desktop), save it to your Desktop and double-click it.

## Emulator Setup

Point your emulator at:
- **IP:** `127.0.0.1`
- **Port:** `26761`

Slot 0 = first controller, Slot 1 = second, etc.

## Works alongside SteamDeckGyroDSU

Run [SteamDeckGyroDSU](https://github.com/kmicki/SteamDeckGyroDSU) on port `26760` to use the Steam Deck's built-in gyro at the same time as your Steam Controllers.

Add both servers in your emulator:
- `127.0.0.1:26760` → Steam Deck gyro
- `127.0.0.1:26761` → Steam Controllers

## Uninstall

Double-click **Uninstall SteamControllerGyroDSU** on your Desktop.

## Notes
- Works via **Bluetooth**, **USB-C**, and **Proteus Puck dongle**
- Gyro auto-calibration activates when the controller is held still for ~2 seconds
- Tested on SteamOS with Eden, Ryujinx, Sudachi, Suyu

## License
MIT
