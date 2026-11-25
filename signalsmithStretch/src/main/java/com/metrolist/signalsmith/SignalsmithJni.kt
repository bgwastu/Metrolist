package com.metrolist.signalsmith

import java.nio.ByteBuffer

internal object SignalsmithJni {
    external fun nativeCreate(
        channels: Int,
        sampleRate: Int,
        isFloat: Boolean,
    ): Long

    external fun nativeDestroy(handle: Long)

    external fun nativeSetParameters(
        handle: Long,
        speed: Float,
        pitch: Float,
    )

    external fun nativeFlush(handle: Long)

    external fun nativeQueueInput(
        handle: Long,
        directBuffer: ByteBuffer,
        offsetBytes: Int,
        lengthBytes: Int,
    )

    external fun nativeQueueEndOfStream(handle: Long)

    external fun nativeAvailableOutputBytes(handle: Long): Int

    external fun nativeReadOutput(
        handle: Long,
        directBuffer: ByteBuffer,
        maxBytes: Int,
    ): Int
}
