#pragma once
#include "triton.h"
#include <hidapi/hidapi.h>
#include <string>
#include <memory>
#include <chrono>

namespace sc2 {

class HidSlot {
public:
    HidSlot(hid_device* dev, int dsuSlot, uint16_t pid, std::string serial);
    ~HidSlot();
    HidSlot(const HidSlot&) = delete;
    HidSlot& operator=(const HidSlot&) = delete;
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
    AxisMap gyroMap_  = DEFAULT_GYRO;
    AxisMap accelMap_ = DEFAULT_ACCEL;
};

class HidManager {
public:
    HidManager();
    ~HidManager();
    void probe();
};

} // namespace sc2
