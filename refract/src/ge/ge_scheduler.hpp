#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

namespace refract::ge {

enum class ListState : std::uint8_t {
  queued,
  running,
  stalled,
  paused,
  completed,
  cancelled,
  error,
};

enum class ListStatus : std::uint32_t {
  completed = 0,
  queued = 1,
  drawing = 2,
  stalled = 3,
  paused = 4,
};

struct CallFrame {
  std::uint32_t return_address{};
  std::uint32_t offset_address{};
};

struct DisplayList {
  int id{};
  std::uint32_t start_address{};
  std::uint32_t program_counter{};
  std::uint32_t stall_address{};
  std::uint32_t callback_id{};
  std::vector<CallFrame> call_stack;
  ListState state{ListState::queued};
  bool bounding_box_visible{};
};

class Scheduler {
public:
  void reset() {
    next_id_ = 1;
    lists_.clear();
    queue_.clear();
  }

  int enqueue(std::uint32_t start_address, std::uint32_t stall_address,
              std::uint32_t callback_id, bool at_head) {
    const auto id = next_id_++;
    lists_.emplace(id, DisplayList{id, start_address, start_address,
                                   stall_address, callback_id, {},
                                   ListState::queued, false});
    if (at_head)
      queue_.push_front(id);
    else
      queue_.push_back(id);
    return id;
  }

  DisplayList* find(int id) {
    const auto found = lists_.find(id);
    return found == lists_.end() ? nullptr : &found->second;
  }

  const DisplayList* find(int id) const {
    const auto found = lists_.find(id);
    return found == lists_.end() ? nullptr : &found->second;
  }

  void begin(int id) {
    if (auto* list = find(id)) list->state = ListState::running;
  }

  void stall(int id, std::uint32_t program_counter,
             std::uint32_t stall_address) {
    if (auto* list = find(id)) {
      list->program_counter = program_counter;
      list->stall_address = stall_address;
      list->state = ListState::stalled;
    }
  }

  void finish(int id, std::uint32_t program_counter, ListState state) {
    if (auto* list = find(id)) {
      list->program_counter = program_counter;
      list->state = state;
    }
    queue_.erase(std::remove(queue_.begin(), queue_.end(), id), queue_.end());
  }

  bool update_stall(int id, std::uint32_t address) {
    auto* list = find(id);
    if (list == nullptr || list->state == ListState::completed ||
        list->state == ListState::cancelled || list->state == ListState::error)
      return false;
    list->stall_address = address;
    if (list->state == ListState::stalled &&
        list->program_counter != address)
      list->state = ListState::queued;
    return true;
  }

  int break_lists(std::uint32_t mode) {
    int current = -1;
    for (const auto id : queue_) {
      auto* list = find(id);
      if (list == nullptr) continue;
      if (current < 0 && (list->state == ListState::running ||
                          list->state == ListState::stalled ||
                          list->state == ListState::queued))
        current = id;
      if (mode == 0U && id != current) continue;
      if (list->state != ListState::completed &&
          list->state != ListState::cancelled)
        list->state = ListState::paused;
      if (mode == 0U) break;
    }
    return current;
  }

  int continue_lists() {
    for (const auto id : queue_) {
      if (auto* list = find(id); list != nullptr &&
          list->state == ListState::paused) {
        list->state = ListState::queued;
        return id;
      }
    }
    return -1;
  }

  ListStatus status(int id) const {
    const auto* list = find(id);
    if (list == nullptr) return ListStatus::completed;
    switch (list->state) {
    case ListState::queued: return ListStatus::queued;
    case ListState::running: return ListStatus::drawing;
    case ListState::stalled: return ListStatus::stalled;
    case ListState::paused: return ListStatus::paused;
    case ListState::completed:
    case ListState::cancelled:
    case ListState::error: return ListStatus::completed;
    }
    return ListStatus::completed;
  }

  ListStatus draw_status() const {
    for (const auto id : queue_) {
      const auto result = status(id);
      if (result != ListStatus::completed) return result;
    }
    return ListStatus::completed;
  }

  bool busy() const { return draw_status() != ListStatus::completed; }

private:
  int next_id_{1};
  std::unordered_map<int, DisplayList> lists_;
  std::deque<int> queue_;
};

} // namespace refract::ge
