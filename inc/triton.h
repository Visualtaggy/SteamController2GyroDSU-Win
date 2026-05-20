#pragma once
#include <cstdint>
#include <array>
#include <string>

namespace sc2 {

constexpr uint16_t VID_VALVE = 0x28DE;

constexpr uint16_t PID_TRITON_WIRED   = 0x1302;
constexpr uint16_t PID_TRITON_BLE     = 0x1303;
constexpr uint16_t PID_PROTEUS_PUCK   = 0x1304;
constexpr uint16_t PID_NEREID_PUCK    = 0x1305;

constexpr std::array<uint16_t, 4> TRITON_PIDS = {
    PID_TRITON_WIRED, PID_TRITON_BLE, PID_PROTEUS_PUCK, PID_NEREID_PUCK
};

constexpr uint8_t REPORT_ID_STATE     = 0x42;
constexpr uint8_t REPORT_ID_STATE_BLE = 0x45;
constexpr uint8_t FEATURE_REPORT_ID   = 0x01;

constexpr uint8_t  ID_SET_SETTINGS     = 0x87;
constexpr uint8_t  SETTING_LIZARD_MODE = 9;
constexpr uint8_t  SETTING_IMU_MODE    = 48;
constexpr uint16_t LIZARD_MODE_OFF     = 0;
constexpr uint16_t IMU_MODE_GYRO_ACCEL = 0x0008 | 0x0010;

constexpr size_t FEATURE_REPORT_SIZE  = 64;
constexpr size_t INPUT_REPORT_SIZE    = 64;
constexpr size_t IMU_OFFSET           = 29;

namespace button {
    constexpr uint32_t A          = 0x0000'0001;
    constexpr uint32_t B          = 0x0000'0002;
    constexpr uint32_t X          = 0x0000'0004;
    constexpr uint32_t Y          = 0x0000'0008;
    constexpr uint32_t QAM        = 0x0000'0010;
    constexpr uint32_t R3         = 0x0000'0020;
    constexpr uint32_t VIEW       = 0x0000'0040;
    constexpr uint32_t R4         = 0x0000'0080;
    constexpr uint32_t R5         = 0x0000'0100;
    constexpr uint32_t R          = 0x0000'0200;
    constexpr uint32_t DPAD_DOWN  = 0x0000'0400;
    constexpr uint32_t DPAD_RIGHT = 0x0000'0800;
    constexpr uint32_t DPAD_LEFT  = 0x0000'1000;
    constexpr uint32_t DPAD_UP    = 0x0000'2000;
    constexpr uint32_t MENU       = 0x0000'4000;
    constexpr uint32_t L3         = 0x0000'8000;
    constexpr uint32_t STEAM      = 0x0001'0000;
    constexpr uint32_t L4         = 0x0002'0000;
    constexpr uint32_t L5         = 0x0004'0000;
    constexpr uint32_t L          = 0x0008'0000;
}

struct ImuSample {
    uint32_t timestamp_us = 0;
    float    accel_g[3]   = {};
    float    gyro_dps[3]  = {};
};

struct ControllerState {
    uint32_t buttons       = 0;
    uint16_t trigger_left  = 0;
    uint16_t trigger_right = 0;
    int16_t  left_stick[2] = {};
    int16_t  right_stick[2]= {};
    ImuSample imu;
    int      slot          = 0;
};

struct AxisMap {
    int src_x = 0; bool inv_x = false;
    int src_y = 1; bool inv_y = false;
    int src_z = 2; bool inv_z = false;

    float applyX(const float raw[3]) const { return inv_x ? -raw[src_x] : raw[src_x]; }
    float applyY(const float raw[3]) const { return inv_y ? -raw[src_y] : raw[src_y]; }
    float applyZ(const float raw[3]) const { return inv_z ? -raw[src_z] : raw[src_z]; }
};

constexpr AxisMap DEFAULT_GYRO  = { 0, false, 1, false, 2, false };
constexpr AxisMap DEFAULT_ACCEL = { 0, false, 1, false, 2, false };

bool isTritonPid(uint16_t pid);
const char* pidLabel(uint16_t pid);

} // namespace sc2
