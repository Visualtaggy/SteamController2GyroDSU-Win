# sc2gyrodsu-win

A DSU (Cemuhook protocol) motion server for the **Steam Controller 2**, ported
to Windows from [Steam-Controller-GyroDSU](https://github.com/dylangmarinus-stack/Steam-Controller-GyroDSU)
by dylangmarinus-stack, which targets SteamOS/Linux.

Streams gyro + accelerometer data (and buttons/sticks/triggers) from up to
4 Steam Controller 2 units over the Cemuhook UDP protocol, so any
DSU-compatible emulator can use the controller's motion sensors.

## Features

- Supports up to **4 Steam Controllers simultaneously** (slots 0-3)
- Works via **Bluetooth**, **USB-C**, and the **Proteus Puck** dongle
- Automatic gyro bias calibration
- Compatible with **Cemu**, **Ryujinx**, **Eden**, and any other
  Cemuhook-compatible emulator

## Quick start (prebuilt exe)

1. Double-click `sc2gyrodsu.exe` (or run it from a terminal to see logs).
2. Connect your Steam Controller 2 via USB-C, Bluetooth, or the Proteus Puck
   dongle.
3. Point your emulator (Cemu, Ryujinx, Eden, ...) at:
   - **IP:** `127.0.0.1`
   - **Port:** `26760` (default) — use `sc2gyrodsu.exe --port 26761` if
     `26760` is already taken by something like DS4Windows or BetterJoy.

Slot 0 = first controller, slot 1 = second, and so on.

### CLI options

| Flag           | Description                                                     |
|----------------|------------------------------------------------------------------|
| `--port N`     | UDP port to listen on (default `26760`)                          |
| `--expose`     | Listen on all interfaces, not just `127.0.0.1` (e.g. for another PC/device on your network) |
| `--probe`      | List detected Valve HID interfaces and exit                      |
| `--help`       | Print usage                                                       |

## Important: Steam interference

If Steam is running, Steam Input may claim the controller and rewrite its
settings (including gyro mode) every few seconds. If you get no motion data:

- Close Steam entirely, **or**
- In Steam → Settings → Controller, disable Steam Input for the Steam
  Controller while using the DSU server.

The server re-sends its IMU-enable command every 100 ms, which usually wins
the tug-of-war, but a clean setup is more reliable.

## Building from source

Requires CMake 3.16+ and either Visual Studio 2019+ or MinGW-w64. hidapi is
fetched and built automatically via `FetchContent` — no manual dependencies.
The resulting `.exe` is fully standalone (statically-linked runtime and
hidapi) either way — no Visual C++ Redistributable or other DLLs required
on the end user's machine.

**Visual Studio (x64 Native Tools prompt):**
```
cmake -B build
cmake --build build --config Release
```
Output: `build\Release\sc2gyrodsu.exe`

**MinGW (or cross-compiling from Linux):**
```
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
A `mingw-toolchain.cmake` toolchain file is included for cross-compiling from
Linux with `x86_64-w64-mingw32-gcc`/`g++`.

The same CMake tree also still builds on Linux (`cmake -B build && cmake --build build`,
using hidapi's `hidraw` backend), so this fork stays upstreamable as a
cross-platform version.

## Run at startup (optional)

Press `Win+R`, type `shell:startup`, and drop a shortcut to `sc2gyrodsu.exe`
in that folder. To hide the console window, launch it via a small `.vbs`
wrapper, or use Task Scheduler with "Run whether user is logged on or not".

## Controller profile (Cemu example)

`controllerProfiles/controller0.xml` is a sample Cemu Wii U GamePad profile
pointed at `127.0.0.1:26760` — copy it into Cemu's `controllerProfiles`
folder as a starting point.

## What changed from the Linux original

- **`inc/platform.h`** (new): Winsock2/POSIX socket abstraction (`socket_t`,
  `closesocket` vs `close`, per-platform `SO_RCVTIMEO`, `WSAStartup` RAII),
  plus a portable `narrow()` for hidapi's `wchar_t*` serial numbers (the
  original `reinterpret_cast` breaks on Windows, where `wchar_t` is UTF-16).
- **`src/dsu.cpp`**: POSIX headers removed; `recvfrom`/`sendto` buffer casts
  and `socklen_t` handled per-platform; `randId()` uses `std::chrono` instead
  of `clock_gettime`/`getpid`, which aren't available on MSVC.
- **`src/hiddev.cpp`**: feature reports retry with a +1-byte buffer, since the
  Windows HID stack (`HidD_SetFeature`) requires the buffer to match the
  device's exact declared feature report length, which differs from hidraw.
- **`CMakeLists.txt`**: fetches hidapi via `FetchContent`, selects the
  `winapi` backend + links `ws2_32` on Windows and `hidraw` on Linux;
  MSVC-compatible compile flags.

Everything else — the HID protocol, DSU packet layout, axis mapping, and
slot management — is unchanged from the original.

## Notes

- Gyro auto-calibration activates when the controller is held still for
  ~2 seconds.
- Only motion data, buttons, sticks, and triggers are provided — no rumble.

## License

MIT — see [LICENSE](LICENSE). Inherited from the original project by
dylangmarinus-stack; this port's changes are released under the same terms.
