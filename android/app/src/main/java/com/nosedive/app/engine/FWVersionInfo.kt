package com.nosedive.app.engine

/**
 * Firmware version info from the C++ engine.
 * Mirrors nd_fw_version_t from ffi.h.
 */
data class FWVersionInfo(
    val hwName: String = "",
    val major: Int = 0,
    val minor: Int = 0,
    val uuid: String = "",
    val hwType: Int = 0,
    val customConfigCount: Int = 0,
    val packageName: String = ""
) {
    val versionString: String get() = "$major.$minor"
}
