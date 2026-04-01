#include <jni.h>
#include "nosedive/ffi.h"
#include <string>
#include <android/log.h>

#define LOG_TAG "NoseDiveJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// JNI bridge for the NoseDive C++ engine.
// Maps 1:1 to the C FFI — Kotlin calls these via NoseDiveEngine.kt.

static nd_engine_t* g_engine = nullptr;

// Callback pointers for JNI -> Kotlin upcalls
static JavaVM* g_jvm = nullptr;
static jobject g_callback_obj = nullptr;

// Helper: attach to JVM if needed, returns env. Sets attached=true if caller must detach.
static JNIEnv* get_env(bool& attached) {
    attached = false;
    if (!g_jvm || !g_callback_obj) return nullptr;
    JNIEnv* env = nullptr;
    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) return env;
    if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    attached = true;
    return env;
}

// Helper: call a void no-arg Kotlin method on g_callback_obj from any thread.
static void call_kotlin_void(const char* method_name) {
    bool attached;
    JNIEnv* env = get_env(attached);
    if (!env) return;
    jclass cls = env->GetObjectClass(g_callback_obj);
    jmethodID mid = env->GetMethodID(cls, method_name, "()V");
    if (env->ExceptionCheck()) { env->ExceptionClear(); mid = nullptr; }
    if (mid) {
        env->CallVoidMethod(g_callback_obj, mid);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    if (attached) g_jvm->DetachCurrentThread();
}

// Helper: call a Kotlin method that takes a byte[] on g_callback_obj from any thread.
static void call_kotlin_bytes(const char* method_name, const uint8_t* data, size_t len) {
    bool attached;
    JNIEnv* env = get_env(attached);
    if (!env) return;
    jclass cls = env->GetObjectClass(g_callback_obj);
    jmethodID mid = env->GetMethodID(cls, method_name, "([B)V");
    if (env->ExceptionCheck()) { env->ExceptionClear(); mid = nullptr; }
    if (mid) {
        jbyteArray arr = env->NewByteArray(static_cast<jsize>(len));
        if (arr) {
            env->SetByteArrayRegion(arr, 0, static_cast<jsize>(len),
                                    reinterpret_cast<const jbyte*>(data));
            env->CallVoidMethod(g_callback_obj, mid, arr);
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(arr);
        }
    }
    env->DeleteLocalRef(cls);
    if (attached) g_jvm->DetachCurrentThread();
}

// C callback: engine wants to send a VESC payload
static void engine_send_cb(const uint8_t* payload, size_t len, void* /*ctx*/) {
    call_kotlin_bytes("onEngineSendPayload", payload, len);
}

// C callback: engine state changed
static void engine_state_cb(void* /*ctx*/) {
    call_kotlin_void("onEngineStateChanged");
}

// C callback: engine diagnostic/error message
static void engine_error_cb(const char* message, void* /*ctx*/) {
    LOGI("Engine error: %s", message);
}

extern "C" {

JNIEXPORT void JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeInit(JNIEnv* env, jobject thiz, jstring storagePath) {
    const char* path = env->GetStringUTFChars(storagePath, nullptr);
    if (!path) return; // OOM
    g_engine = nd_engine_create(path);
    env->ReleaseStringUTFChars(storagePath, path);
    env->GetJavaVM(&g_jvm);

    // Store a global ref to the engine object for upcalls from C++ callbacks
    if (g_callback_obj) {
        env->DeleteGlobalRef(g_callback_obj);
    }
    g_callback_obj = env->NewGlobalRef(thiz);

    // Set engine callbacks so on_connected() discovery commands actually go somewhere
    if (g_engine) {
        nd_engine_set_send_callback(g_engine, engine_send_cb, nullptr);
        nd_engine_set_state_callback(g_engine, engine_state_cb, nullptr);
        nd_engine_set_error_callback(g_engine, engine_error_cb, nullptr);
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
    if (g_engine) t = nd_engine_get_telemetry(g_engine);

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

// Refloat info — returns [name, major, minor, patch, suffix] or null if no refloat
JNIEXPORT jobjectArray JNICALL
Java_com_nosedive_app_engine_NoseDiveEngine_nativeGetRefloatInfo(JNIEnv* env, jobject) {
    if (!g_engine || !nd_engine_has_refloat(g_engine)) return nullptr;
    nd_refloat_info_t ri = nd_engine_get_refloat_info(g_engine);

    jclass strClass = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray(5, strClass, nullptr);
    if (!arr) return nullptr;

    env->SetObjectArrayElement(arr, 0, env->NewStringUTF(ri.name));
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", ri.major);
    env->SetObjectArrayElement(arr, 1, env->NewStringUTF(buf));
    snprintf(buf, sizeof(buf), "%d", ri.minor);
    env->SetObjectArrayElement(arr, 2, env->NewStringUTF(buf));
    snprintf(buf, sizeof(buf), "%d", ri.patch);
    env->SetObjectArrayElement(arr, 3, env->NewStringUTF(buf));
    env->SetObjectArrayElement(arr, 4, env->NewStringUTF(ri.suffix));

    env->DeleteLocalRef(strClass);
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
