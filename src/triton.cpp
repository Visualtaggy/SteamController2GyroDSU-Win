#include "triton.h"
#include <algorithm>

namespace sc2 {

bool isTritonPid(uint16_t pid) {
    return std::find(TRITON_PIDS.begin(), TRITON_PIDS.end(), pid) != TRITON_PIDS.end();
}

const char* pidLabel(uint16_t pid) {
    switch (pid) {
        case PID_TRITON_WIRED: return "Triton wired";
        case PID_TRITON_BLE:   return "Triton BLE";
        case PID_PROTEUS_PUCK: return "Proteus Puck";
        case PID_NEREID_PUCK:  return "Nereid Puck";
        default:               return "unknown";
    }
}

} // namespace sc2
