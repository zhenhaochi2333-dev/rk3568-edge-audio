#include "edgeaudio/asr_engine.h"

#include <chrono>
#include <cstring>
#include <utility>

#if EDGEAUDIO_HAS_SHERPA
#include <sherpa-onnx/c-api/c-api.h>
#endif

namespace edgeaudio {

AsrEngine::AsrEngine(Config config) : config_(std::move(config)) {
#if EDGEAUDIO_HAS_SHERPA
    SherpaOnnxOnlineRecognizerConfig config{};
    config.feat_config.sample_rate = kSampleRate;
    config.feat_config.feature_dim = 80;
    config.model_config.transducer.encoder = config_.encoder.c_str();
    config.model_config.transducer.decoder = config_.decoder.c_str();
    config.model_config.transducer.joiner = config_.joiner.c_str();
    config.model_config.tokens = config_.tokens.c_str();
    config.model_config.num_threads = config_.threads;
    config.model_config.provider = config_.backend == "rknn" ? "rknn" : "cpu";
    config.model_config.model_type = "zipformer";
    config.model_config.modeling_unit = "cjkchar";
    config.decoding_method = "greedy_search";
    config.enable_endpoint = config_.endpoint_detection ? 1 : 0;
    config.rule1_min_trailing_silence = 2.4f;
    config.rule2_min_trailing_silence = 1.2f;
    config.rule3_min_utterance_length = 20.0f;
    const auto* recognizer = SherpaOnnxCreateOnlineRecognizer(&config);
    if (!recognizer) {
        error_ = "SherpaOnnxCreateOnlineRecognizer failed";
        return;
    }
    recognizer_ = const_cast<SherpaOnnxOnlineRecognizer*>(recognizer);
    const auto* stream = SherpaOnnxCreateOnlineStream(recognizer);
    if (!stream) {
        error_ = "SherpaOnnxCreateOnlineStream failed";
        SherpaOnnxDestroyOnlineRecognizer(recognizer);
        recognizer_ = nullptr;
        return;
    }
    stream_ = const_cast<SherpaOnnxOnlineStream*>(stream);
#else
    error_ = "ASR runtime not linked; configure SHERPA_ONNX_ROOT and rebuild";
#endif
}

AsrEngine::~AsrEngine() {
#if EDGEAUDIO_HAS_SHERPA
    if (stream_) SherpaOnnxDestroyOnlineStream(static_cast<SherpaOnnxOnlineStream*>(stream_));
    if (recognizer_) SherpaOnnxDestroyOnlineRecognizer(static_cast<SherpaOnnxOnlineRecognizer*>(recognizer_));
#endif
}

bool AsrEngine::available() const { return recognizer_ != nullptr && stream_ != nullptr; }

bool AsrEngine::feed(const std::int16_t* samples, std::size_t count,
                     std::uint64_t first_sample, const ResultCallback& callback) {
#if EDGEAUDIO_HAS_SHERPA
    if (!available()) return false;
    std::vector<float> normalized(count);
    for (std::size_t i = 0; i < count; ++i) normalized[i] = static_cast<float>(samples[i]) / 32768.0f;
    const auto start = std::chrono::steady_clock::now();
    const auto* stream = static_cast<const SherpaOnnxOnlineStream*>(stream_);
    auto* recognizer = static_cast<const SherpaOnnxOnlineRecognizer*>(recognizer_);
    SherpaOnnxOnlineStreamAcceptWaveform(stream, kSampleRate, normalized.data(), static_cast<int32_t>(count));
    while (SherpaOnnxIsOnlineStreamReady(recognizer, stream)) SherpaOnnxDecodeOnlineStream(recognizer, stream);
    const auto* result = SherpaOnnxGetOnlineStreamResult(recognizer, stream);
    if (result) {
        AsrResult output;
        output.timestamp_ms = (first_sample + count) * 1000 / kSampleRate;
        output.text = result->text ? result->text : "";
        output.final = false;
        output.latency_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        output.rtf = count == 0 ? 0.0 : output.latency_ms / (count * 1000.0 / kSampleRate);
        const bool endpoint = config_.endpoint_detection &&
            SherpaOnnxOnlineStreamIsEndpoint(recognizer, stream);
        if (output.text != last_text_ || endpoint) {
            last_text_ = output.text;
            output.final = endpoint;
            callback(output);
        }
        SherpaOnnxDestroyOnlineRecognizerResult(result);
        if (endpoint) {
            SherpaOnnxOnlineStreamReset(recognizer, stream);
            last_text_.clear();
        }
    }
    return true;
#else
    (void)samples; (void)count; (void)first_sample; (void)callback;
    return false;
#endif
}

bool AsrEngine::finish(std::uint64_t first_sample, const ResultCallback& callback) {
#if EDGEAUDIO_HAS_SHERPA
    if (!available()) return false;
    const auto* stream = static_cast<const SherpaOnnxOnlineStream*>(stream_);
    auto* recognizer = static_cast<const SherpaOnnxOnlineRecognizer*>(recognizer_);
    SherpaOnnxOnlineStreamInputFinished(stream);
    while (SherpaOnnxIsOnlineStreamReady(recognizer, stream)) SherpaOnnxDecodeOnlineStream(recognizer, stream);
    const auto* result = SherpaOnnxGetOnlineStreamResult(recognizer, stream);
    if (result && result->text && std::strlen(result->text) > 0) {
        AsrResult output;
        output.timestamp_ms = first_sample * 1000 / kSampleRate;
        output.text = result->text;
        output.final = true;
        callback(output);
        SherpaOnnxDestroyOnlineRecognizerResult(result);
    } else if (result) {
        SherpaOnnxDestroyOnlineRecognizerResult(result);
    }
    SherpaOnnxOnlineStreamReset(recognizer, stream);
    last_text_.clear();
    return true;
#else
    (void)first_sample; (void)callback;
    return false;
#endif
}

void AsrEngine::reset() {
#if EDGEAUDIO_HAS_SHERPA
    if (available()) SherpaOnnxOnlineStreamReset(
        static_cast<const SherpaOnnxOnlineRecognizer*>(recognizer_),
        static_cast<const SherpaOnnxOnlineStream*>(stream_));
#endif
    last_text_.clear();
}

}  // namespace edgeaudio
