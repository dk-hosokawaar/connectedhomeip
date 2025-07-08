#pragma once
#include <cstdint> 
namespace aircon_bridge {
void Init();
void SendControl(uint8_t mode, int16_t hundredthDegC);
inline void SendSetpoint(int16_t hundredthDegC) {    // 既存呼び出し維持
    extern uint8_t sLastMode;        // thermostat_handler.cpp で定義
    SendControl(sLastMode, hundredthDegC);
}
}  // namespace aircon_bridge
