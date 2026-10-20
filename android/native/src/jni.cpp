//JNI bridge between the Kotlin app (com.micronut99.ares.NativeBridge) and the frontend.
//
//Threads:
//  - the emulation thread (owned here) runs frames, paced by the audio buffer;
//  - GLSurfaceView's render thread calls nativeDraw*;
//  - AAudio's callback thread pulls samples;
//  - the UI thread calls everything else. Anything that touches the core while it
//    runs (save states, reset, unload) is queued to the emulation thread and waited for.

#include <jni.h>
#include <android/log.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>

#include "audio-output.hpp"
#include "frontend.hpp"
#include "gl-renderer.hpp"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "ares", __VA_ARGS__)

using namespace aresdroid;

namespace {

struct Emulator {
  Session session;
  AudioOutput audio;
  GLRenderer renderer;
  bool audioOpen = false;

  std::thread thread;
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<std::function<void()>> jobs;
  std::atomic<bool> running{false};   //the emulation thread exists
  std::atomic<bool> paused{true};
  std::atomic<bool> fastForward{false};

  std::mutex logMutex;
  std::deque<std::string> logLines;

  std::atomic<uint32_t> framesThisSecond{0};
  std::atomic<uint32_t> fps{0};

  void log(const std::string& line) {
    LOG("%s", line.c_str());
    std::lock_guard<std::mutex> lock(logMutex);
    logLines.push_back(line);
    while(logLines.size() > 200) logLines.pop_front();
  }

  //run `job` on the emulation thread and wait for it
  template<typename T> T call(std::function<T()> job) {
    if(!running) return job();
    auto task = std::make_shared<std::packaged_task<T()>>(std::move(job));
    auto result = task->get_future();
    {
      std::lock_guard<std::mutex> lock(mutex);
      jobs.push_back([task] { (*task)(); });
    }
    wake.notify_all();
    return result.get();
  }

  void loop() {
    using clock = std::chrono::steady_clock;
    auto secondStart = clock::now();
    auto nextFrame = clock::now();
    while(running) {
      std::function<void()> job;
      {
        std::unique_lock<std::mutex> lock(mutex);
        if(jobs.empty() && (paused || !session.loaded())) {
          wake.wait_for(lock, std::chrono::milliseconds(50));
        }
        if(!jobs.empty()) {
          job = std::move(jobs.front());
          jobs.pop_front();
        }
      }
      if(job) { job(); continue; }
      if(paused || !session.loaded()) { nextFrame = clock::now(); continue; }

      //pacing: keep ~70 ms of audio queued. Without audio, pace by the clock at 60 Hz.
      bool due;
      if(fastForward) due = true;
      else if(audioOpen) due = session.bufferedAudioFrames() < audio.sampleRate() * 7 / 100;
      else due = clock::now() >= nextFrame;
      if(!due) {
        std::this_thread::sleep_for(std::chrono::microseconds(500));
        continue;
      }
      session.runFrame();
      nextFrame += std::chrono::microseconds(16639);
      if(clock::now() - nextFrame > std::chrono::milliseconds(100)) nextFrame = clock::now();

      framesThisSecond++;
      if(clock::now() - secondStart >= std::chrono::seconds(1)) {
        fps = framesThisSecond.exchange(0);
        secondStart = clock::now();
      }
    }
  }

  void start() {
    if(running) return;
    running = true;
    thread = std::thread([this] { loop(); });
  }

  void stop() {
    if(!running) return;
    running = false;
    wake.notify_all();
    if(thread.joinable()) thread.join();
  }
};

Emulator* emulator = nullptr;

std::string string(JNIEnv* env, jstring value) {
  if(!value) return {};
  const char* chars = env->GetStringUTFChars(value, nullptr);
  std::string result = chars ? chars : "";
  if(chars) env->ReleaseStringUTFChars(value, chars);
  return result;
}

jstring jstr(JNIEnv* env, const std::string& value) {
  return env->NewStringUTF(value.c_str());
}

}

#define JNI(name) extern "C" JNIEXPORT auto JNICALL Java_com_micronut99_ares_NativeBridge_##name

JNI(nativeInit)(JNIEnv*, jclass) -> void {
  if(emulator) return;
  emulator = new Emulator;
  emulator->session.setLogger([](const std::string& line) { if(emulator) emulator->log(line); });
  emulator->session.setSampleRate(48000);
  emulator->audioOpen = emulator->audio.open([](float* out, uint32_t frames) {
    emulator->session.readAudio(out, frames);
  });
  if(!emulator->audioOpen) emulator->log("No audio output: running without sound");
  emulator->start();
}

JNI(nativeLoad)(JNIEnv* env, jclass, jstring home, jstring game, jstring disc, jint machine, jstring region,
                jstring biosUS, jstring biosJP, jstring biosEU, jstring ramCartFile, jint ramCartSize,
                jboolean writeProtect, jboolean recompiler) -> jstring {
  if(!emulator) return jstr(env, "not initialized");
  Paths paths;
  paths.home = string(env, home);
  paths.biosUS = string(env, biosUS);
  paths.biosJP = string(env, biosJP);
  paths.biosEU = string(env, biosEU);
  LoadRequest request;
  request.game = string(env, game);
  request.disc = string(env, disc);
  request.machine = (Machine)machine;
  request.region = string(env, region);
  request.ramCart.file = string(env, ramCartFile);
  request.ramCart.sizeMiB = (uint32_t)ramCartSize;
  request.ramCart.writeProtect = writeProtect;
  request.recompiler = recompiler;
  emulator->paused = true;
  auto error = emulator->call<std::string>([=] { return emulator->session.load(paths, request); });
  if(!error.empty()) emulator->log("Load failed: " + error);
  return error.empty() ? nullptr : jstr(env, error);
}

JNI(nativeUnload)(JNIEnv*, jclass) -> void {
  if(!emulator) return;
  emulator->paused = true;
  emulator->call<int>([] { emulator->session.unload(); return 0; });
}

JNI(nativeSetPaused)(JNIEnv*, jclass, jboolean paused) -> void {
  if(!emulator) return;
  emulator->paused = paused;
  emulator->audio.setPaused(paused);
  emulator->wake.notify_all();
}

JNI(nativeSetFastForward)(JNIEnv*, jclass, jboolean enabled) -> void {
  if(emulator) emulator->fastForward = enabled;
}

JNI(nativeSetButtons)(JNIEnv*, jclass, jint port, jint mask) -> void {
  if(emulator) emulator->session.setButtons((uint32_t)port, (uint32_t)mask);
}

JNI(nativeReset)(JNIEnv*, jclass) -> void {
  if(!emulator) return;
  emulator->call<int>([] { emulator->session.reset(); return 0; });
}

JNI(nativeSaveMemory)(JNIEnv*, jclass) -> void {
  if(!emulator) return;
  emulator->call<int>([] { emulator->session.saveMemory(); return 0; });
}

JNI(nativeSaveState)(JNIEnv* env, jclass, jstring file) -> jboolean {
  if(!emulator) return false;
  auto path = string(env, file);
  return emulator->call<bool>([path] { return emulator->session.saveState(path); });
}

JNI(nativeLoadState)(JNIEnv* env, jclass, jstring file) -> jboolean {
  if(!emulator) return false;
  auto path = string(env, file);
  return emulator->call<bool>([path] { return emulator->session.loadState(path); });
}

JNI(nativeStatus)(JNIEnv* env, jclass) -> jstring {
  if(!emulator || !emulator->session.loaded()) return jstr(env, "");
  char text[160];
  snprintf(text, sizeof(text), "%u fps  STATE %04X", emulator->fps.load(), emulator->session.comm(6));
  return jstr(env, text);
}

JNI(nativeDescription)(JNIEnv* env, jclass) -> jstring {
  if(!emulator) return jstr(env, "");
  return jstr(env, emulator->session.description());
}

JNI(nativeTakeLog)(JNIEnv* env, jclass) -> jstring {
  if(!emulator) return jstr(env, "");
  std::lock_guard<std::mutex> lock(emulator->logMutex);
  std::string all;
  for(auto& line : emulator->logLines) all += line + "\n";
  emulator->logLines.clear();
  return jstr(env, all);
}

JNI(nativeSurfaceCreated)(JNIEnv*, jclass) -> void {
  if(emulator) emulator->renderer.surfaceCreated();
}

JNI(nativeSurfaceChanged)(JNIEnv*, jclass, jint width, jint height) -> void {
  if(emulator) emulator->renderer.surfaceChanged(width, height);
}

JNI(nativeDraw)(JNIEnv*, jclass) -> void {
  if(emulator) emulator->renderer.draw(emulator->session);
}

JNI(nativeSetVideo)(JNIEnv*, jclass, jint scale, jboolean smooth) -> void {
  if(!emulator) return;
  emulator->renderer.scale = (GLRenderer::Scale)scale;
  emulator->renderer.smooth = smooth;
}
