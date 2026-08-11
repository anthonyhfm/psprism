#pragma once

#if !defined(__PSP__)

#include <mutex>
#include <unordered_map>

namespace thread_status {

struct Snapshot {
  std::int32_t status{1};
  std::int32_t wait_type{};
  std::int32_t wait_id{};
};

inline std::mutex& mutex() {
  // Guest threads are joined from Runtime's static destructor.  Keep this
  // bookkeeping alive until process teardown has fully stopped those threads.
  static auto* value = new std::mutex;
  return *value;
}

inline std::unordered_map<int, Snapshot>& snapshots() {
  static auto* value = new std::unordered_map<int, Snapshot>;
  return *value;
}

inline void set_running(int uid) {
  std::lock_guard lock(mutex());
  snapshots()[uid] = Snapshot{};
}

inline void set_waiting(int uid, int wait_type, int wait_id) {
  std::lock_guard lock(mutex());
  snapshots()[uid] = Snapshot{4, wait_type, wait_id};
}

inline Snapshot get(int uid) {
  std::lock_guard lock(mutex());
  if (const auto found = snapshots().find(uid); found != snapshots().end())
    return found->second;
  return {};
}

class ScopedWait {
public:
  ScopedWait(int uid, int wait_type, int wait_id) : uid_(uid) {
    set_waiting(uid, wait_type, wait_id);
  }

  ~ScopedWait() { set_running(uid_); }

  ScopedWait(const ScopedWait&) = delete;
  ScopedWait& operator=(const ScopedWait&) = delete;

private:
  int uid_{};
};

} // namespace thread_status

#endif
