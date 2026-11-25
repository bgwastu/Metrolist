#ifndef SIGNALSMITH_STRETCH_SESSION_H
#define SIGNALSMITH_STRETCH_SESSION_H

#include "signalsmith-stretch/signalsmith-stretch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace metrolist {
namespace signalsmith_jni {

class StretchSession {
public:
    static constexpr int kOutputChunkFrames = 256;
    static constexpr float kTonalityLimitHz = 8000.0f;

    StretchSession(int channels, int sampleRate, bool isFloat)
        : channels_(channels),
          sampleRate_(sampleRate),
          isFloat_(isFloat),
          sampleBytes_(isFloat ? static_cast<int>(sizeof(float)) : static_cast<int>(sizeof(int16_t))),
          frameBytes_(channels * sampleBytes_),
          inputChannels_(static_cast<size_t>(channels)),
          chunkIn_(static_cast<size_t>(channels)),
          chunkOut_(static_cast<size_t>(channels)),
          chunkInPtrs_(static_cast<size_t>(channels), nullptr),
          chunkOutPtrs_(static_cast<size_t>(channels), nullptr) {
        stretch_.presetDefault(channels_, static_cast<float>(sampleRate_), false);
        applyTranspose();
    }

    void setParameters(float speed, float pitch) {
        speed_ = std::max(0.1f, std::min(8.0f, speed));
        pitch_ = std::max(0.1f, std::min(8.0f, pitch));
        applyTranspose();
    }

    void flush() {
        stretch_.reset();
        applyTranspose();
        for (auto& ch : inputChannels_) {
            ch.clear();
        }
        inputReadPos_ = 0;
        outputBytes_.clear();
        outputReadPos_ = 0;
        inputPhase_ = 0.0;
        seekDone_ = false;
        seekFramesHeld_ = 0;
        ended_ = false;
    }

    void queueInput(const uint8_t* data, int lengthBytes) {
        if (lengthBytes <= 0 || frameBytes_ <= 0) return;
        const int numFrames = lengthBytes / frameBytes_;
        if (numFrames <= 0) return;

        for (int c = 0; c < channels_; ++c) {
            auto& ch = inputChannels_[static_cast<size_t>(c)];
            ch.reserve(ch.size() + static_cast<size_t>(numFrames));
        }

        if (isFloat_) {
            const auto* src = reinterpret_cast<const float*>(data);
            for (int i = 0; i < numFrames; ++i) {
                for (int c = 0; c < channels_; ++c) {
                    inputChannels_[static_cast<size_t>(c)].push_back(src[i * channels_ + c]);
                }
            }
        } else {
            const auto* src = reinterpret_cast<const int16_t*>(data);
            constexpr float kScale = 1.0f / 32768.0f;
            for (int i = 0; i < numFrames; ++i) {
                for (int c = 0; c < channels_; ++c) {
                    inputChannels_[static_cast<size_t>(c)].push_back(
                        static_cast<float>(src[i * channels_ + c]) * kScale);
                }
            }
        }

        processAvailable();
    }

    void queueEndOfStream() {
        if (ended_) return;
        ended_ = true;
        processAvailable();

        const int remaining = availableInputFrames();
        const auto toOutputFrames = [this](double frames) {
            return static_cast<int>(std::llround(frames / speed_));
        };
        if (!seekDone_) {
            if (remaining == 0) return;
            // Media3 can drain during a speed change before the entire preroll arrives.
            const int seekLength = stretch_.outputSeekLength(speed_);
            prepareInputChunk(remaining, seekLength);
            stretch_.outputSeek(chunkInPtrs_.data(), seekLength);
            const int tail = toOutputFrames(remaining);
            prepareOutputChunk(tail);
            stretch_.flush(chunkOutPtrs_.data(), tail, speed_);
            appendOutput(tail);
            return;
        }

        const int frames = std::max(0, toOutputFrames(remaining - inputPhase_));
        if (remaining > 0) {
            prepareInputChunk(remaining);
            prepareOutputChunk(frames);
            stretch_.process(chunkInPtrs_.data(), remaining, chunkOutPtrs_.data(), frames);
            appendOutput(frames);
        }
        const int tail = toOutputFrames(remaining + seekFramesHeld_ - inputPhase_) - frames;
        prepareOutputChunk(tail);
        stretch_.flush(chunkOutPtrs_.data(), tail, speed_);
        appendOutput(tail);
    }

    int readOutput(uint8_t* dst, int maxBytes) {
        const size_t avail = availableOutputBytes();
        if (avail == 0 || maxBytes <= 0) return 0;
        const size_t toCopy = std::min(avail, static_cast<size_t>(maxBytes));
        std::memcpy(dst, outputBytes_.data() + outputReadPos_, toCopy);
        outputReadPos_ += toCopy;
        if (outputReadPos_ >= outputBytes_.size()) {
            outputBytes_.clear();
            outputReadPos_ = 0;
        } else if (outputReadPos_ >= 32768) {
            outputBytes_.erase(outputBytes_.begin(), outputBytes_.begin() + static_cast<std::ptrdiff_t>(outputReadPos_));
            outputReadPos_ = 0;
        }
        return static_cast<int>(toCopy);
    }

    int availableOutputBytes() const {
        return static_cast<int>(outputBytes_.size() - outputReadPos_);
    }

private:
    int channels_;
    int sampleRate_;
    bool isFloat_;
    int sampleBytes_;
    int frameBytes_;
    float speed_ = 1.0f;
    float pitch_ = 1.0f;
    double inputPhase_ = 0.0;
    bool seekDone_ = false;
    int seekFramesHeld_ = 0;
    bool ended_ = false;

    signalsmith::stretch::SignalsmithStretch<float> stretch_;
    std::vector<std::vector<float>> inputChannels_;
    size_t inputReadPos_ = 0;
    std::vector<std::vector<float>> chunkIn_;
    std::vector<std::vector<float>> chunkOut_;
    std::vector<float*> chunkInPtrs_;
    std::vector<float*> chunkOutPtrs_;
    std::vector<uint8_t> outputBytes_;
    size_t outputReadPos_ = 0;

    void applyTranspose() {
        const float tonalityLimit = kTonalityLimitHz / static_cast<float>(sampleRate_);
        stretch_.setTransposeFactor(pitch_, tonalityLimit);
    }

    int availableInputFrames() const {
        if (inputChannels_.empty()) return 0;
        return static_cast<int>(inputChannels_[0].size() - inputReadPos_);
    }

    void compactInputIfNeeded() {
        if (inputReadPos_ < 4096) return;
        for (auto& ch : inputChannels_) {
            ch.erase(ch.begin(), ch.begin() + static_cast<std::ptrdiff_t>(inputReadPos_));
        }
        inputReadPos_ = 0;
    }

    void prepareInputChunk(int frames, int paddedFrames = 0) {
        for (int c = 0; c < channels_; ++c) {
            auto& dst = chunkIn_[static_cast<size_t>(c)];
            dst.resize(static_cast<size_t>(frames));
            const auto& src = inputChannels_[static_cast<size_t>(c)];
            std::memcpy(dst.data(), src.data() + inputReadPos_, static_cast<size_t>(frames) * sizeof(float));
            if (paddedFrames > frames) dst.resize(static_cast<size_t>(paddedFrames), 0.0f);
            chunkInPtrs_[static_cast<size_t>(c)] = dst.data();
        }
        inputReadPos_ += static_cast<size_t>(frames);
        compactInputIfNeeded();
    }

    void prepareOutputChunk(int frames) {
        for (int c = 0; c < channels_; ++c) {
            auto& dst = chunkOut_[static_cast<size_t>(c)];
            dst.resize(static_cast<size_t>(frames));
            chunkOutPtrs_[static_cast<size_t>(c)] = dst.data();
        }
    }

    void appendOutput(int frames) {
        if (frames <= 0) return;
        const size_t oldSize = outputBytes_.size();
        const size_t addBytes = static_cast<size_t>(frames) * static_cast<size_t>(frameBytes_);
        outputBytes_.resize(oldSize + addBytes);
        uint8_t* dstBytes = outputBytes_.data() + oldSize;

        if (isFloat_) {
            auto* dst = reinterpret_cast<float*>(dstBytes);
            for (int i = 0; i < frames; ++i) {
                for (int c = 0; c < channels_; ++c) {
                    dst[i * channels_ + c] = chunkOut_[static_cast<size_t>(c)][static_cast<size_t>(i)];
                }
            }
        } else {
            auto* dst = reinterpret_cast<int16_t*>(dstBytes);
            for (int i = 0; i < frames; ++i) {
                for (int c = 0; c < channels_; ++c) {
                    float v = chunkOut_[static_cast<size_t>(c)][static_cast<size_t>(i)];
                    if (v > 1.0f) v = 1.0f;
                    if (v < -1.0f) v = -1.0f;
                    long scaled = std::lrintf(v * 32767.0f);
                    if (scaled < -32768) scaled = -32768;
                    if (scaled > 32767) scaled = 32767;
                    dst[i * channels_ + c] = static_cast<int16_t>(scaled);
                }
            }
        }
    }

    void processAvailable() {
        if (!seekDone_) {
            const int seekLen = std::max(1, stretch_.outputSeekLength(speed_));
            if (availableInputFrames() < seekLen) return;
            prepareInputChunk(seekLen);
            applyTranspose();
            stretch_.outputSeek(chunkInPtrs_.data(), seekLen);
            seekDone_ = true;
            seekFramesHeld_ = seekLen;
            inputPhase_ = 0.0;
        }

        while (true) {
            const double exactIn =
                static_cast<double>(kOutputChunkFrames) * static_cast<double>(speed_) + inputPhase_;
            const int inFrames = std::max(1, static_cast<int>(std::floor(exactIn + 1e-9)));
            if (availableInputFrames() < inFrames) break;
            inputPhase_ = exactIn - static_cast<double>(inFrames);
            prepareInputChunk(inFrames);
            prepareOutputChunk(kOutputChunkFrames);
            stretch_.process(chunkInPtrs_.data(), inFrames, chunkOutPtrs_.data(), kOutputChunkFrames);
            appendOutput(kOutputChunkFrames);
        }
    }
};

}  // namespace signalsmith_jni
}  // namespace metrolist

#endif  // SIGNALSMITH_STRETCH_SESSION_H
