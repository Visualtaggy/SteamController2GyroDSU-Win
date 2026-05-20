#pragma once
#include "triton.h"
#include <hidapi/hidapi.h>
#include <functional>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <chrono>

namespace sc2 {

// One open connection to one physical Steam Controller.
class HidSlot {
public:
    HidSlot(hid_device* dev, int dsuSlot, uint16_t pid, std::string serial);
    ~HidSlot();

    // Non-copyable
    HidSlot(const HidSlot&) = delete;
    HidSlot& operator=(const HidSlot&) = delete;

    // Read one HID frame. Returns true if a valid ControllerState was decoded.
    bool readOne(ControllerState& out);

    int         dsuSlot;
    uint16_t    pid;
    std::string serial;

private:
    void sendFeatureReport(uint8_t setting, uint16_t value);
    void refreshLizard();
    void refreshImu();

    hid_device*  dev_;
    std::chrono::steady_clock::time_point lastLizard_;
    std::chrono::steady_clock::time_point lastImu_;
    AxisMap      gyroMap_  = DEFAULT_GYRO;
    AxisMap      accelMap_ = DEFAULT_ACCEL;
    uint32_t     lastTs_   = 0;
    bool         hasLastTs_= false;
};

// Manages scanning and opening of all connected SC2 controllers.
class HidManager {
public:
    HidManager();
    ~HidManager();

    // Called from controller thread. Scans for new devices and opens them
    // into free DSU slots. Returns list of newly opened slots.
    // Removes dead slots from active list.
    void scan(std::vector<std::unique_ptr<HidSlot>>& active,
              bool slotUsed[4]);

    void probe(); // Print all Valve HID devices

private:
    // Returns true if a device with this serial is already in active.
    static bool isActive(const std::vector<std::unique_ptr<HidSlot>>& active,
                         const std::string& serial);
};

} // namespace sc2
