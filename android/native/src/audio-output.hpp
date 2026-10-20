#pragma once

#include <aaudio/AAudio.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

namespace aresdroid {

//AAudio output stream. Pulls interleaved stereo float frames from `source`.
//Reopens itself when the device changes (headphones, Bluetooth).
class AudioOutput {
public:
  using Source = std::function<void(float* out, uint32_t frames)>;

  bool open(Source source);   //returns false if no audio device is available
  void close();
  uint32_t sampleRate() const { return rate; }
  void setPaused(bool paused);

private:
  static aaudio_data_callback_result_t dataCallback(AAudioStream*, void* user, void* audioData, int32_t frames);
  static void errorCallback(AAudioStream*, void* user, aaudio_result_t error);
  bool openStream();

  std::mutex mutex;
  AAudioStream* stream = nullptr;
  Source source;
  uint32_t rate = 48000;
  std::atomic<bool> paused{false};
};

}
