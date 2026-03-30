#pragma once

#include <cstdint>
#include <cstddef>

namespace nosedive {

// CRC-16/CCITT (XModem) as used by VESC firmware.
uint16_t crc16(const uint8_t* data, size_t len);

} // namespace nosedive
