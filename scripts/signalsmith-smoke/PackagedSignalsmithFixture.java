import java.io.ByteArrayOutputStream;
import java.io.File;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.util.Arrays;

public class PackagedSignalsmithFixture {
    private static final int RATE = 48000;
    private static final int CHANNELS = 2;
    private static final int FRAME_BYTES = 4;
    private static final int BLOCK_BYTES = 191 * FRAME_BYTES;
    private static final float[] SPEEDS = { 0.75f, 1.25f };
    private static final String[] LABELS = { "slow.pcm", "fast.pcm" };

    public static void main(String[] args) throws Exception {
        File directory = new File(args[1]);
        byte[] input = Files.readAllBytes(new File(directory, "input.pcm").toPath());
        if (args[0].equals("android")) {
            System.load(args[2]);
            tryAndroid(directory, input);
        } else {
            tryDesktop(directory, input);
        }
    }

    private static void tryDesktop(File directory, byte[] input) throws Exception {
        String prefix = "com.metrolist.shared.playback.ffmpeg.";
        Class<?> loader = Class.forName(prefix + "DesktopSignalsmith");
        if (!(Boolean) loader.getMethod("isAvailable").invoke(loader.getField("INSTANCE").get(null))) {
            throw new AssertionError("Signalsmith was not loaded from the package");
        }
        System.out.println("Desktop package loaded its Signalsmith library");
        Class<?> processor = Class.forName(prefix + "AudioTempoProcessor");
        Class<?> clockClass = Class.forName(prefix + "AudioPositionClock");
        Object clock = clockClass.getConstructor(float.class).newInstance((float) RATE);
        clockClass.getMethod("reset", long.class, long.class, float.class).invoke(clock, 0L, 0L, SPEEDS[0]);
        Method position = clockClass.getMethod("position", long.class);
        Class<?> callbackClass = Class.forName("kotlin.jvm.functions.Function1");
        Method process = processor.getMethod("process", byte[].class, callbackClass);
        Object unit = Class.forName("kotlin.Unit").getField("INSTANCE").get(null);
        long boundary = 0;
        for (int i = 0; i < SPEEDS.length; i++) {
            ByteArrayOutputStream sink = new ByteArrayOutputStream();
            Object sinkCallback = Proxy.newProxyInstance(callbackClass.getClassLoader(), new Class<?>[]{callbackClass},
                (proxy, method, callbackArgs) -> {
                    sink.write((byte[]) callbackArgs[0]);
                    return unit;
                });
            try (AutoCloseable stretch = (AutoCloseable) processor.getConstructor(int.class, int.class, float.class, float.class)
                .newInstance(RATE, CHANNELS, SPEEDS[i], 1f)) {
                for (int offset = 0; offset < input.length; offset += BLOCK_BYTES) {
                    process.invoke(stretch, Arrays.copyOfRange(input, offset, Math.min(input.length, offset + BLOCK_BYTES)), sinkCallback);
                }
                process.invoke(stretch, null, sinkCallback);
            }
            long cueFrame = boundary + Math.round(RATE / (double) SPEEDS[i]);
            long mediaMs = (Long) position.invoke(clock, cueFrame);
            long expectedMs = 1000L + 2000L * i;
            if (Math.abs(mediaMs - expectedMs) > 1) throw new AssertionError("Clock mapped cue to " + mediaMs + " ms");
            System.out.println("Desktop position clock maps cue " + (i + 1) + " to " + mediaMs + " ms");
            Files.write(new File(directory, LABELS[i]).toPath(), sink.toByteArray());
            boundary += sink.size() / FRAME_BYTES;
            if (i + 1 < SPEEDS.length) {
                clockClass.getMethod("changeSpeed", long.class, float.class).invoke(clock, boundary, SPEEDS[i + 1]);
                long lastOldPosition = (Long) position.invoke(clock, boundary - 1);
                long atBoundary = (Long) position.invoke(clock, boundary);
                if (lastOldPosition != 1999L || atBoundary != 2000L) {
                    throw new AssertionError("Buffered speed transition mapped to " + lastOldPosition + ", " + atBoundary);
                }
                System.out.println("Desktop clock advances from " + lastOldPosition + " to " + atBoundary + " ms at the speed boundary");
            }
        }
    }

    private static void tryAndroid(File directory, byte[] input) throws Exception {
        Class<?> processorClass = Class.forName("com.metrolist.signalsmith.SignalsmithAudioProcessor");
        Class<?> formatClass = Class.forName("androidx.media3.common.audio.AudioProcessor\u0024AudioFormat");
        Class<?> metadataClass = Class.forName("androidx.media3.common.audio.AudioProcessor\u0024StreamMetadata");
        Object format = formatClass.getConstructor(int.class, int.class, int.class).newInstance(RATE, CHANNELS, 2);
        Object metadata = metadataClass.getField("DEFAULT").get(null);
        Object processor = processorClass.getConstructor().newInstance();
        Method queue = processorClass.getMethod("queueInput", ByteBuffer.class);
        Method output = processorClass.getMethod("getOutput");
        System.out.println("Android loaded the arm64 library extracted from the debug APK");
        try {
            processorClass.getMethod("configure", formatClass).invoke(processor, format);
            for (int i = 0; i < SPEEDS.length; i++) {
                ByteArrayOutputStream sink = new ByteArrayOutputStream();
                processorClass.getMethod("setPlaybackParameters", float.class, float.class).invoke(processor, SPEEDS[i], 1f);
                processorClass.getMethod("flush", metadataClass).invoke(processor, metadata);
                ByteBuffer block = ByteBuffer.allocateDirect(BLOCK_BYTES).order(ByteOrder.nativeOrder());
                for (int offset = 0; offset < input.length; offset += BLOCK_BYTES) {
                    block.clear();
                    block.put(input, offset, Math.min(BLOCK_BYTES, input.length - offset));
                    block.flip();
                    queue.invoke(processor, block);
                    if (block.hasRemaining()) throw new AssertionError("PCM was not consumed");
                    write(sink, (ByteBuffer) output.invoke(processor));
                }
                processorClass.getMethod("queueEndOfStream").invoke(processor);
                while (!(Boolean) processorClass.getMethod("isEnded").invoke(processor)) {
                    ByteBuffer tail = (ByteBuffer) output.invoke(processor);
                    if (!tail.hasRemaining()) throw new AssertionError("Playback stalled while draining");
                    write(sink, tail);
                }
                long playoutUs = Math.round((long) sink.size() * 1_000_000.0 / (FRAME_BYTES * RATE));
                long mediaUs = (Long) processorClass.getMethod("getMediaDuration", long.class).invoke(processor, playoutUs);
                if (Math.abs(mediaUs - 2_000_000L) > 2) throw new AssertionError("Media duration mismatch " + mediaUs);
                System.out.println("Android processed " + mediaUs + " µs of media at " + SPEEDS[i] + "x");
                Files.write(new File(directory, LABELS[i]).toPath(), sink.toByteArray());
            }
        } finally {
            processorClass.getMethod("reset").invoke(processor);
        }
    }

    private static void write(ByteArrayOutputStream sink, ByteBuffer output) throws Exception {
        byte[] bytes = new byte[output.remaining()];
        output.get(bytes);
        sink.write(bytes);
    }
}
