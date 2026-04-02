#include "vesc/vescpkg.hpp"
#include <cstring>
#include <zlib.h>

namespace vesc {

// Decompress zlib data (raw deflate or zlib-wrapped).
static std::vector<uint8_t> zlib_decompress(const uint8_t* data, size_t len) {
    std::vector<uint8_t> out;
    out.resize(len * 4); // initial guess

    z_stream strm{};
    // windowBits=15+32 to auto-detect zlib/gzip header
    if (inflateInit2(&strm, 15 + 32) != Z_OK) return {};

    strm.next_in = const_cast<Bytef*>(data);
    strm.avail_in = static_cast<uInt>(len);

    int ret;
    do {
        strm.next_out = out.data() + strm.total_out;
        strm.avail_out = static_cast<uInt>(out.size() - strm.total_out);

        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_BUF_ERROR || (ret == Z_OK && strm.avail_out == 0)) {
            out.resize(out.size() * 2);
        } else if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&strm);
            return {};
        }
    } while (ret != Z_STREAM_END);

    out.resize(strm.total_out);
    inflateEnd(&strm);
    return out;
}

// Read a big-endian uint32 from data.
static uint32_t read_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  | uint32_t(p[3]);
}

std::optional<VescPackage> VescPackage::parse(const uint8_t* data, size_t len) {
    if (len < 4) return std::nullopt;

    // Decompress everything after the 4-byte header
    auto decompressed = zlib_decompress(data + 4, len - 4);
    if (decompressed.empty()) return std::nullopt;

    VescPackage pkg;
    size_t pos = 0;
    const uint8_t* d = decompressed.data();
    size_t dlen = decompressed.size();

    // Parse entries: key\0 + 4-byte-BE-length + value
    while (pos < dlen) {
        // Find null terminator for key
        const uint8_t* key_start = d + pos;
        const uint8_t* null_pos = static_cast<const uint8_t*>(
            std::memchr(key_start, 0, dlen - pos));
        if (!null_pos) break;

        std::string key(reinterpret_cast<const char*>(key_start),
                        null_pos - key_start);
        pos = (null_pos - d) + 1; // past null

        // Read 4-byte BE length
        if (pos + 4 > dlen) break;
        uint32_t val_len = read_be32(d + pos);
        pos += 4;

        if (pos + val_len > dlen) break;

        if (key == "name") {
            pkg.name = std::string(reinterpret_cast<const char*>(d + pos), val_len);
        } else if (key == "lispData") {
            pkg.lisp_data.assign(d + pos, d + pos + val_len);
        } else if (key == "qmlFile") {
            pkg.qml_data.assign(d + pos, d + pos + val_len);
        }
        // Skip unknown keys

        pos += val_len;
    }

    if (pkg.lisp_data.empty() && pkg.qml_data.empty()) return std::nullopt;
    return pkg;
}

} // namespace vesc
