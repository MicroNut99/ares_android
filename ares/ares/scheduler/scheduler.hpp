struct Thread;

struct Scheduler {
  enum class Mode : u32 {
    Run,
    Synchronize,
    SynchronizePrimary,
    SynchronizeAuxiliary,
  };

  Scheduler() = default;
  Scheduler(const Scheduler&) = delete;
  auto operator=(const Scheduler&) = delete;

  auto reset() -> void;
  auto threads() const -> u32;
  auto thread(u32 threadID) const -> maybe<Thread&>;
  auto uniqueID() const -> u32;
  auto minimum() const -> u64;
  auto maximum() const -> u64;

  auto append(Thread& thread) -> bool;
  auto remove(Thread& thread) -> void;

  auto power(Thread& thread) -> void;
  auto enter(Mode mode = Mode::Run) -> Event;
  auto exit(Event event) -> void;

  //(fpga-cartfx branch) everything exit() has taken off the threads' clocks since power(), in nanoseconds (a u64 of
  //Thread::Second units holds only two seconds): a thread's clock() / NanoUnits plus this is a time in nanoseconds
  //that never goes back (the cart compositor's model times its engine with it)
  static constexpr u64 NanoUnits = ((u64)-1 >> 1) / 1'000'000'000;
  auto reducedNs() const -> u64 { return _reducedNs; }

  auto synchronizing() const -> bool;
  auto synchronize() -> void;

  auto getSynchronize() -> bool;
  auto setSynchronize(bool) -> void;

private:
  cothread_t _host = nullptr;     //program thread (used to exit scheduler)
  cothread_t _resume = nullptr;   //resume thread (used to enter scheduler)
  cothread_t _primary = nullptr;  //primary thread (used to synchronize components)
  u64 _reducedNs = 0;
  Mode _mode = Mode::Run;
  Event _event = Event::Step;
  std::vector<Thread*> _threads;
  bool _synchronize = false;

  friend struct Thread;
};

extern Scheduler scheduler;
