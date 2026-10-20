#include "audio-output.hpp"

#include <android/log.h>

#include <cstring>
#include <thread>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "ares", __VA_ARGS__)

namespace aresdroid {

bool AudioOutput::open(Source newSource) {
  std::lock_guard<std::mutex> lock(mutex);
  source = std::move(newSource);
  return openStream();
}

bool AudioOutput::openStream() {
  AAudioStreamBuilder* builder = nullptr;
  if(AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return false;
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setChannelCount(builder, 2);
  //a fixed rate keeps the emulator's resampler simple; AAudio converts if the device differs
  AAudioStreamBuilder_setSampleRate(builder, 48000);
  AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
  AAudioStreamBuilder_setDataCallback(builder, dataCallback, this);
  AAudioStreamBuilder_setErrorCallback(builder, errorCallback, this);
  aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &stream);
  AAudioStreamBuilder_delete(builder);
  if(result != AAUDIO_OK) {
    LOG("AAudio open failed: %s", AAudio_convertResultToText(result));
    stream = nullptr;
    return false;
  }
  rate = (uint32_t)AAudioStream_getSampleRate(stream);
  //two bursts of buffering: low latency, few underruns
  int32_t burst = AAudioStream_getFramesPerBurst(stream);
  if(burst > 0) AAudioStream_setBufferSizeInFrames(stream, burst * 2);
  AAudioStream_requestStart(stream);
  LOG("AAudio started: %u Hz, burst %d", rate, burst);
  return true;
}

void AudioOutput::close() {
  std::lock_guard<std::mutex> lock(mutex);
  if(stream) {
    AAudioStream_requestStop(stream);
    AAudioStream_close(stream);
    stream = nullptr;
  }
}

void AudioOutput::setPaused(bool value) {
  paused = value;
}

aaudio_data_callback_result_t AudioOutput::dataCallback(AAudioStream*, void* user, void* audioData, int32_t frames) {
  auto self = (AudioOutput*)user;
  auto out = (float*)audioData;
  if(self->paused || !self->source) {
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
  } else {
    self->source(out, (uint32_t)frames);
  }
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void AudioOutput::errorCallback(AAudioStream*, void* user, aaudio_result_t error) {
  //the stream cannot be reopened from this callback's thread
  auto self = (AudioOutput*)user;
  if(error != AAUDIO_ERROR_DISCONNECTED) return;
  std::thread([self] {
    std::lock_guard<std::mutex> lock(self->mutex);
    if(self->stream) {
      AAudioStream_requestStop(self->stream);
      AAudioStream_close(self->stream);
      self->stream = nullptr;
    }
    self->openStream();
  }).detach();
}

}
