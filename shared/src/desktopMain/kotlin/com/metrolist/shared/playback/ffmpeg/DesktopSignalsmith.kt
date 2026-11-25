package com.metrolist.shared.playback.ffmpeg

import java.nio.file.Files
import java.nio.file.StandardCopyOption

internal object DesktopSignalsmithJni {
    external fun nativeCreate(
        channels: Int,
        sampleRate: Int,
    ): Long

    external fun nativeDestroy(handle: Long)

    external fun nativeSetParameters(
        handle: Long,
        speed: Float,
        pitch: Float,
    )

    external fun nativeQueueInput(
        handle: Long,
        input: ByteArray,
        lengthBytes: Int,
    )

    external fun nativeQueueEndOfStream(handle: Long)

    external fun nativeAvailableOutputBytes(handle: Long): Int

    external fun nativeReadOutput(
        handle: Long,
        output: ByteArray,
        maxBytes: Int,
    ): Int
}

internal object DesktopSignalsmith {
    val isAvailable: Boolean by lazy { loadNativeLibrary() }

    private fun loadNativeLibrary(): Boolean {
        val osName = System.getProperty("os.name").orEmpty().lowercase()
        val archName = System.getProperty("os.arch").orEmpty().lowercase()
        val os =
            when {
                osName.contains("mac") || osName.contains("darwin") -> "macos"
                osName.contains("linux") -> "linux"
                osName.contains("win") -> "windows"
                else -> return false
            }
        val arch =
            when {
                archName.contains("aarch64") || archName.contains("arm64") -> "aarch64"
                archName.contains("x86_64") || archName.contains("amd64") -> "x86_64"
                else -> return false
            }
        val (fileName, suffix) =
            when (os) {
                "macos" -> "libsignalsmithStretchDesktopJNI.dylib" to ".dylib"
                "linux" -> "libsignalsmithStretchDesktopJNI.so" to ".so"
                else -> "signalsmithStretchDesktopJNI.dll" to ".dll"
            }
        val resourcePath = "/signalsmith/native/$os-$arch/$fileName"
        return try {
            val stream = DesktopSignalsmith::class.java.getResourceAsStream(resourcePath) ?: return false
            val tempFile = Files.createTempFile("signalsmithStretchDesktopJNI-", suffix)
            tempFile.toFile().deleteOnExit()
            stream.use { Files.copy(it, tempFile, StandardCopyOption.REPLACE_EXISTING) }
            System.load(tempFile.toAbsolutePath().toString())
            true
        } catch (_: Throwable) {
            false
        }
    }
}
