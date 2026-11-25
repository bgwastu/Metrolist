#include "stretch_session.h"

#include <jni.h>
#include <algorithm>
#include <cstdint>
#include <vector>

using metrolist::signalsmith_jni::StretchSession;

namespace {

inline StretchSession* fromHandle(jlong handle) {
    return reinterpret_cast<StretchSession*>(static_cast<intptr_t>(handle));
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_metrolist_shared_playback_ffmpeg_DesktopSignalsmithJni_nativeCreate(
    JNIEnv*,
    jclass,
    jint channels,
    jint sampleRate) {
    if (channels <= 0 || sampleRate <= 0) return 0;
    auto* session = new StretchSession(channels, sampleRate, false);
    return static_cast<jlong>(reinterpret_cast<intptr_t>(session));
}

JNIEXPORT void JNICALL
Java_com_metrolist_shared_playback_ffmpeg_DesktopSignalsmithJni_nativeDestroy(
    JNIEnv*,
    jclass,
    jlong handle) {
    auto* session = fromHandle(handle);
    delete session;
}

JNIEXPORT void JNICALL
Java_com_metrolist_shared_playback_ffmpeg_DesktopSignalsmithJni_nativeSetParameters(
    JNIEnv*,
    jclass,
    jlong handle,
    jfloat speed,
    jfloat pitch) {
    auto* session = fromHandle(handle);
    if (session) session->setParameters(speed, pitch);
}

JNIEXPORT void JNICALL
Java_com_metrolist_shared_playback_ffmpeg_DesktopSignalsmithJni_nativeQueueInput(
    JNIEnv* env,
    jclass,
    jlong handle,
    jbyteArray inputArray,
    jint lengthBytes) {
    auto* session = fromHandle(handle);
    if (!session || !inputArray || lengthBytes <= 0) return;
    const jsize arrayLen = env->GetArrayLength(inputArray);
    const int capped = std::min<int>(static_cast<int>(arrayLen), lengthBytes);
    if (capped <= 0) return;
    jbyte* ptr = env->GetByteArrayElements(inputArray, nullptr);
    if (!ptr) return;
    session->queueInput(reinterpret_cast<const uint8_t*>(ptr), capped);
    env->ReleaseByteArrayElements(inputArray, ptr, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_metrolist_shared_playback_ffmpeg_DesktopSignalsmithJni_nativeQueueEndOfStream(
    JNIEnv*,
    jclass,
    jlong handle) {
    auto* session = fromHandle(handle);
    if (session) session->queueEndOfStream();
}

JNIEXPORT jint JNICALL
Java_com_metrolist_shared_playback_ffmpeg_DesktopSignalsmithJni_nativeAvailableOutputBytes(
    JNIEnv*,
    jclass,
    jlong handle) {
    auto* session = fromHandle(handle);
    return session ? session->availableOutputBytes() : 0;
}

JNIEXPORT jint JNICALL
Java_com_metrolist_shared_playback_ffmpeg_DesktopSignalsmithJni_nativeReadOutput(
    JNIEnv* env,
    jclass,
    jlong handle,
    jbyteArray outputArray,
    jint maxBytes) {
    auto* session = fromHandle(handle);
    if (!session || !outputArray || maxBytes <= 0) return 0;
    const jsize arrayLen = env->GetArrayLength(outputArray);
    const int capped = std::min<int>(static_cast<int>(arrayLen), maxBytes);
    if (capped <= 0) return 0;
    std::vector<uint8_t> tmp(static_cast<size_t>(capped));
    const int read = session->readOutput(tmp.data(), capped);
    if (read > 0) {
        env->SetByteArrayRegion(outputArray, 0, read, reinterpret_cast<const jbyte*>(tmp.data()));
    }
    return read;
}

}  // extern "C"
