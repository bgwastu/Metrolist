#include "stretch_session.h"

#include <jni.h>
#include <cstdint>

using metrolist::signalsmith_jni::StretchSession;

namespace {

inline StretchSession* fromHandle(jlong handle) {
    return reinterpret_cast<StretchSession*>(static_cast<intptr_t>(handle));
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_metrolist_signalsmith_SignalsmithJni_nativeCreate(
    JNIEnv*,
    jclass,
    jint channels,
    jint sampleRate,
    jboolean isFloat) {
    if (channels <= 0 || sampleRate <= 0) return 0;
    auto* session = new StretchSession(channels, sampleRate, isFloat == JNI_TRUE);
    return static_cast<jlong>(reinterpret_cast<intptr_t>(session));
}

JNIEXPORT void JNICALL
Java_com_metrolist_signalsmith_SignalsmithJni_nativeDestroy(
    JNIEnv*,
    jclass,
    jlong handle) {
    auto* session = fromHandle(handle);
    delete session;
}

JNIEXPORT void JNICALL
Java_com_metrolist_signalsmith_SignalsmithJni_nativeSetParameters(
    JNIEnv*,
    jclass,
    jlong handle,
    jfloat speed,
    jfloat pitch) {
    auto* session = fromHandle(handle);
    if (session) session->setParameters(speed, pitch);
}

JNIEXPORT void JNICALL
Java_com_metrolist_signalsmith_SignalsmithJni_nativeFlush(
    JNIEnv*,
    jclass,
    jlong handle) {
    auto* session = fromHandle(handle);
    if (session) session->flush();
}

JNIEXPORT void JNICALL
Java_com_metrolist_signalsmith_SignalsmithJni_nativeQueueInput(
    JNIEnv* env,
    jclass,
    jlong handle,
    jobject directBuffer,
    jint offsetBytes,
    jint lengthBytes) {
    auto* session = fromHandle(handle);
    if (!session || !directBuffer || lengthBytes <= 0 || offsetBytes < 0) return;
    auto* base = static_cast<const uint8_t*>(env->GetDirectBufferAddress(directBuffer));
    if (!base) return;
    const jlong capacity = env->GetDirectBufferCapacity(directBuffer);
    if (capacity < static_cast<jlong>(offsetBytes) + static_cast<jlong>(lengthBytes)) return;
    session->queueInput(base + offsetBytes, lengthBytes);
}

JNIEXPORT void JNICALL
Java_com_metrolist_signalsmith_SignalsmithJni_nativeQueueEndOfStream(
    JNIEnv*,
    jclass,
    jlong handle) {
    auto* session = fromHandle(handle);
    if (session) session->queueEndOfStream();
}

JNIEXPORT jint JNICALL
Java_com_metrolist_signalsmith_SignalsmithJni_nativeAvailableOutputBytes(
    JNIEnv*,
    jclass,
    jlong handle) {
    auto* session = fromHandle(handle);
    return session ? session->availableOutputBytes() : 0;
}

JNIEXPORT jint JNICALL
Java_com_metrolist_signalsmith_SignalsmithJni_nativeReadOutput(
    JNIEnv* env,
    jclass,
    jlong handle,
    jobject directBuffer,
    jint maxBytes) {
    auto* session = fromHandle(handle);
    if (!session || !directBuffer || maxBytes <= 0) return 0;
    auto* base = static_cast<uint8_t*>(env->GetDirectBufferAddress(directBuffer));
    if (!base) return 0;
    const jlong capacity = env->GetDirectBufferCapacity(directBuffer);
    const int capped = static_cast<int>(std::min<jlong>(capacity, maxBytes));
    return session->readOutput(base, capped);
}

}  // extern "C"
