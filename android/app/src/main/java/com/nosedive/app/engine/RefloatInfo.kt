package com.nosedive.app.engine

/**
 * Refloat package info from the C++ engine.
 * Mirrors nd_refloat_info_t from ffi.h.
 */
data class RefloatInfo(
    val name: String = "",
    val major: Int = 0,
    val minor: Int = 0,
    val patch: Int = 0,
    val suffix: String = ""
) {
    val versionString: String
        get() {
            val base = "$major.$minor.$patch"
            return if (suffix.isNotEmpty()) "$base-$suffix" else base
        }
}
