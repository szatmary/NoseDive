#pragma once

// VESC Package (.vescpkg) parser.
//
// File format: 4-byte header ("VESC") + zlib-compressed "VESC Packet".
// VESC Packet: sequence of entries, each: key\0 + 4-byte-BE-length + value.
//
// Refloat packages contain "lispData" and "qmlFile" entries.

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace vesc {

struct VescPackage {
    std::string name;
    std::vector<uint8_t> lisp_data;
    std::vector<uint8_t> qml_data;

    /// Parse a .vescpkg file from raw bytes. Returns nullopt on failure.
    static std::optional<VescPackage> parse(const uint8_t* data, size_t len);
};

} // namespace vesc
