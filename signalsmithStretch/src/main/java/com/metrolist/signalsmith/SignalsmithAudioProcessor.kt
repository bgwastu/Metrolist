package com.metrolist.signalsmith

import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.Format
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.audio.AudioProcessor.AudioFormat
import androidx.media3.common.audio.AudioProcessor.StreamMetadata
import androidx.media3.common.audio.AudioProcessor.UnhandledAudioFormatException
import androidx.media3.common.util.UnstableApi
import java.nio.ByteBuffer
import java.nio.ByteOrder

@OptIn(UnstableApi::class)
class SignalsmithAudioProcessor : AudioProcessor {
    private var speed = 1f
    private var pitch = 1f

    private var pendingInputAudioFormat = AudioFormat.NOT_SET
    private var inputAudioFormat = AudioFormat.NOT_SET

    private var buffer: ByteBuffer = AudioProcessor.EMPTY_BUFFER
    private var outputBuffer: ByteBuffer = AudioProcessor.EMPTY_BUFFER
    private var scratchInputBuffer: ByteBuffer = AudioProcessor.EMPTY_BUFFER

    private var nativeHandle = 0L
    private var inputEnded = false

    fun setPlaybackParameters(
        speed: Float,
        pitch: Float,
    ) {
        this.speed = speed
        this.pitch = pitch
    }

    // Each flushed segment produces exactly input duration / speed, within one PCM frame.
    fun getMediaDuration(playoutDuration: Long): Long = (playoutDuration * speed.toDouble()).toLong()

    override fun getDurationAfterProcessorApplied(durationUs: Long): Long = (durationUs / speed.toDouble()).toLong()

    @Throws(UnhandledAudioFormatException::class)
    override fun configure(inputAudioFormat: AudioFormat): AudioFormat {
        if (inputAudioFormat.encoding != C.ENCODING_PCM_16BIT && inputAudioFormat.encoding != C.ENCODING_PCM_FLOAT) {
            throw UnhandledAudioFormatException(inputAudioFormat)
        }
        pendingInputAudioFormat = inputAudioFormat
        return inputAudioFormat
    }

    override fun isActive(): Boolean = pendingInputAudioFormat.sampleRate != Format.NO_VALUE && (speed != 1f || pitch != 1f)

    override fun queueInput(inputBuffer: ByteBuffer) {
        if (!inputBuffer.hasRemaining()) return
        val handle = nativeHandle
        if (handle == 0L) return
        val remaining = inputBuffer.remaining()
        if (inputBuffer.isDirect) {
            SignalsmithJni.nativeQueueInput(handle, inputBuffer, inputBuffer.position(), remaining)
        } else {
            if (scratchInputBuffer.capacity() < remaining) {
                scratchInputBuffer = ByteBuffer.allocateDirect(remaining).order(ByteOrder.nativeOrder())
            } else {
                scratchInputBuffer.clear()
            }
            scratchInputBuffer.put(inputBuffer.duplicate())
            scratchInputBuffer.flip()
            SignalsmithJni.nativeQueueInput(handle, scratchInputBuffer, 0, remaining)
        }
        inputBuffer.position(inputBuffer.limit())
    }

    override fun queueEndOfStream() {
        val handle = nativeHandle
        if (handle != 0L) {
            SignalsmithJni.nativeQueueEndOfStream(handle)
        }
        inputEnded = true
    }

    override fun getOutput(): ByteBuffer {
        val handle = nativeHandle
        if (handle != 0L) {
            val available = SignalsmithJni.nativeAvailableOutputBytes(handle)
            if (available > 0) {
                if (buffer.capacity() < available) {
                    buffer = ByteBuffer.allocateDirect(available).order(ByteOrder.nativeOrder())
                } else {
                    buffer.clear()
                }
                val read = SignalsmithJni.nativeReadOutput(handle, buffer, available)
                buffer.position(read)
                buffer.flip()
                outputBuffer = buffer
            }
        }
        val result = outputBuffer
        outputBuffer = AudioProcessor.EMPTY_BUFFER
        return result
    }

    override fun isEnded(): Boolean {
        val handle = nativeHandle
        return inputEnded && (handle == 0L || SignalsmithJni.nativeAvailableOutputBytes(handle) == 0)
    }

    override fun flush(streamMetadata: StreamMetadata) {
        if (isActive()) {
            val formatChanged = inputAudioFormat != pendingInputAudioFormat
            inputAudioFormat = pendingInputAudioFormat
            if (formatChanged || nativeHandle == 0L) {
                destroyNative()
                nativeHandle =
                    SignalsmithJni.nativeCreate(
                        inputAudioFormat.channelCount,
                        inputAudioFormat.sampleRate,
                        inputAudioFormat.encoding == C.ENCODING_PCM_FLOAT,
                    )
            } else {
                SignalsmithJni.nativeFlush(nativeHandle)
            }
            val handle = nativeHandle
            if (handle != 0L) {
                SignalsmithJni.nativeSetParameters(handle, speed, pitch)
            }
        }
        outputBuffer = AudioProcessor.EMPTY_BUFFER
        inputEnded = false
    }

    override fun reset() {
        speed = 1f
        pitch = 1f
        pendingInputAudioFormat = AudioFormat.NOT_SET
        inputAudioFormat = AudioFormat.NOT_SET
        buffer = AudioProcessor.EMPTY_BUFFER
        outputBuffer = AudioProcessor.EMPTY_BUFFER
        scratchInputBuffer = AudioProcessor.EMPTY_BUFFER
        destroyNative()
        inputEnded = false
    }

    private fun destroyNative() {
        val handle = nativeHandle
        if (handle != 0L) {
            nativeHandle = 0L
            SignalsmithJni.nativeDestroy(handle)
        }
    }

    protected fun finalize() {
        destroyNative()
    }
}
