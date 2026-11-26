#include "stretch_session.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int sampleRate = 48000;
constexpr int segmentFrames = 2 * sampleRate;
constexpr int channels = 2;
constexpr int cueFrame = sampleRate;
constexpr double pi = 3.141592653589793;
constexpr std::array<double, 2> speeds{0.75, 1.25};
using Pcm = std::vector<int16_t>;
using Segments = std::array<Pcm, 2>;

Pcm makeFixture() {
    Pcm samples(segmentFrames * channels);
    for (int frame = 0; frame < segmentFrames; ++frame) {
        const double time = double(frame) / sampleRate;
        const double cueDistance = (frame - cueFrame) / (0.013 * sampleRate);
        samples[frame * channels] = int16_t(8500 * std::sin(2 * pi * 440 * time));
        samples[frame * channels + 1] =
            int16_t(18000 * std::exp(-0.5 * cueDistance * cueDistance) * std::sin(2 * pi * 1370 * time));
    }
    return samples;
}

double cueCenter(const Pcm& pcm) {
    double weightedFrame = 0;
    double energy = 0;
    for (size_t frame = 0; frame < pcm.size() / channels; ++frame) {
        const double amplitude = pcm[frame * channels + 1];
        const double power = amplitude * amplitude;
        energy += power;
        weightedFrame += power * frame;
    }
    if (energy < 1e6) throw std::runtime_error("Missing audio cue");
    return weightedFrame / energy;
}

double pitchHz(const Pcm& pcm) {
    const int begin = sampleRate / 4;
    const int end = sampleRate / 2;
    int crossings = 0;
    for (int frame = begin + 1; frame <= end; ++frame) {
        if (pcm[(frame - 1) * channels] <= 0 && pcm[frame * channels] > 0) ++crossings;
    }
    return double(crossings) * sampleRate / (end - begin);
}

void verify(const Segments& rendered) {
    double playbackFrame = 0;
    for (size_t i = 0; i < rendered.size(); ++i) {
        const auto& output = rendered[i];
        const auto expectedFrames = std::lround(segmentFrames / speeds[i]);
        const auto actualFrames = long(output.size() / channels);
        if (std::abs(actualFrames - expectedFrames) > 1 || output.size() % channels) {
            throw std::runtime_error("Wrong output duration for segment " + std::to_string(i + 1));
        }
        const double expectedCue = playbackFrame + cueFrame / speeds[i];
        const double actualCue = playbackFrame + cueCenter(output);
        const double offsetMs = (actualCue - expectedCue) * 1000 / sampleRate;
        const double frequency = pitchHz(output);
        std::cout << speeds[i] << "x: " << actualFrames << " frames, pitch " << frequency
                  << " Hz, cue at " << actualCue / sampleRate << " s (offset " << offsetMs << " ms)\n";
        if (std::abs(frequency - 440) > 8) throw std::runtime_error("Pitch changed with speed");
        if (std::abs(offsetMs) > 35) throw std::runtime_error("Cue exceeds 35 ms timing tolerance");
        playbackFrame += actualFrames;
    }
}

Segments process(const Pcm& fixture) {
    metrolist::signalsmith_jni::StretchSession processor(channels, sampleRate, false);
    Segments output;
    for (size_t i = 0; i < speeds.size(); ++i) {
        processor.flush();
        processor.setParameters(float(speeds[i]), 1);
        constexpr int inputBlockFrames = 191;
        for (int frame = 0; frame < segmentFrames; frame += inputBlockFrames) {
            const int frames = std::min(segmentFrames - frame, inputBlockFrames);
            processor.queueInput(reinterpret_cast<const uint8_t*>(fixture.data() + frame * channels), frames * channels * sizeof(int16_t));
        }
        processor.queueEndOfStream();
        const int availableBytes = processor.availableOutputBytes();
        output[i].resize(availableBytes / sizeof(int16_t));
        if (processor.readOutput(reinterpret_cast<uint8_t*>(output[i].data()), availableBytes) != availableBytes) {
            throw std::runtime_error("Incomplete audio output");
        }
    }
    return output;
}

Pcm readPcm(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || file.tellg() % sizeof(int16_t)) throw std::runtime_error("Invalid PCM: " + path.string());
    Pcm samples(size_t(file.tellg()) / sizeof(int16_t));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(samples.data()), samples.size() * sizeof(int16_t))) {
        throw std::runtime_error("Cannot read PCM: " + path.string());
    }
    return samples;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            verify(process(makeFixture()));
        } else if (argc == 3 && std::string(argv[1]) == "generate") {
            const auto fixture = makeFixture();
            std::ofstream file(argv[2], std::ios::binary);
            if (!file.write(reinterpret_cast<const char*>(fixture.data()), fixture.size() * sizeof(int16_t))) {
                throw std::runtime_error("Cannot write fixture");
            }
        } else if (argc == 4 && std::string(argv[1]) == "check") {
            verify({readPcm(argv[2]), readPcm(argv[3])});
        } else {
            throw std::runtime_error("Usage: signalsmith-fixture [generate INPUT | check SLOW FAST]");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
