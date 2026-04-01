#include <jni.h>
#include "nosedive/ffi.h"
#include <string>

// JNI bridge for the NoseDive C++ engine.
// Maps 1:1 to the C FFI — Kotlin calls these via NoseDiveEngine.kt.

static nd_engine_t* g_engine = nullptr;

// Callback pointers for JNI → Kotlin upcalls
static JavaVM* g_jvm = nullptr;
static jobject g_callback_obj = nullptr;

extern "C" {

JNIEXPORT void JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeInit(JNIEnv* env, jobject, jstring storagePath) {
    const char* path = env->GetStringUTFChars(storagePath, nullptr);
    if (!path) return; // OOM
    g_engine = nd_engine_create(path);
    env->ReleaseStringUTFChars(storagePath, path);
    env->GetJavaVM(&g_jvm);
}

JNIEXPORT void JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeDestroy(JNIEnv*, jobject) {
    if (g_engine) {
        nd_engine_destroy(g_engine);
        g_engine = nullptr;
    }
}

JNIEXPORT void JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeOnConnected(JNIEnv*, jobject) {
    if (g_engine) nd_engine_on_connected(g_engine);
}

JNIEXPORT void JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeOnDisconnected(JNIEnv*, jobject) {
    if (g_engine) nd_engine_on_disconnected(g_engine);
}

JNIEXPORT void JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeHandlePayload(JNIEnv* env, jobject, jbyteArray data) {
    if (!g_engine) return;
    jsize len = env->GetArrayLength(data);
    jbyte* bytes = env->GetByteArrayElements(data, nullptr);
    if (!bytes) return; // OOM
    nd_engine_handle_payload(g_engine, reinterpret_cast<const uint8_t*>(bytes), len);
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
}

JNIEXPORT jboolean JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeHasActiveBoard(JNIEnv*, jobject) {
    return g_engine ? nd_engine_has_active_board(g_engine) : false;
}

JNIEXPORT jboolean JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeHasRefloat(JNIEnv*, jobject) {
    return g_engine ? nd_engine_has_refloat(g_engine) : false;
}

JNIEXPORT void JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeInstallRefloat(JNIEnv*, jobject) {
    if (g_engine) nd_engine_install_refloat(g_engine);
}

JNIEXPORT jboolean JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeShouldShowWizard(JNIEnv*, jobject) {
    return g_engine ? nd_engine_should_show_wizard(g_engine) : false;
}

JNIEXPORT void JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeDismissWizard(JNIEnv*, jobject) {
    if (g_engine) nd_engine_dismiss_wizard(g_engine);
}

JNIEXPORT jdouble JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeSpeedKmh(JNIEnv*, jobject) {
    return g_engine ? nd_engine_speed_kmh(g_engine) : 0.0;
}

JNIEXPORT jdouble JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeSpeedMph(JNIEnv*, jobject) {
    return g_engine ? nd_engine_speed_mph(g_engine) : 0.0;
}

// Telemetry — returns as a flat double array for efficiency
JNIEXPORT jdoubleArray JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeGetTelemetry(JNIEnv* env, jobject) {
    nd_telemetry_t t = {};
    if (g_engine) nd_engine_get_telemetry(g_engine, &t);

    jdoubleArray arr = env->NewDoubleArray(13);
    if (!arr) return nullptr; // OOM
    jdouble vals[] = {
        t.temp_mosfet, t.temp_motor, t.motor_current, t.battery_current,
        t.duty_cycle, t.erpm, t.battery_voltage, t.battery_percent,
        t.speed, t.power,
        static_cast<double>(t.tachometer), static_cast<double>(t.tachometer_abs),
        static_cast<double>(t.fault)
    };
    env->SetDoubleArrayRegion(arr, 0, 13, vals);
    return arr;
}

JNIEXPORT jint JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeBoardCount(JNIEnv*, jobject) {
    return g_engine ? static_cast<jint>(nd_engine_board_count(g_engine)) : 0;
}

JNIEXPORT jint JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeProfileCount(JNIEnv*, jobject) {
    return g_engine ? static_cast<jint>(nd_engine_profile_count(g_engine)) : 0;
}

} // extern "C"
