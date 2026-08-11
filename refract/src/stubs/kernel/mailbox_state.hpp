#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>

namespace mailbox_state {

constexpr std::uint32_t message_priority_attribute = 0x400U;

struct Message {
  std::uint32_t address{};
  std::uint8_t priority{};
};

inline bool enqueue(std::deque<Message>& messages, std::uint32_t attributes,
                    std::uint32_t address, std::uint8_t priority) {
  if (std::find_if(messages.begin(), messages.end(),
                   [address](const Message& message) {
                     return message.address == address;
                   }) != messages.end()) {
    return false;
  }
  const Message message{address, priority};
  if ((attributes & message_priority_attribute) == 0U) {
    messages.push_back(message);
    return true;
  }
  const auto found = std::find_if(
      messages.begin(), messages.end(), [priority](const Message& queued) {
        return queued.priority > priority;
      });
  messages.insert(found, message);
  return true;
}

} // namespace mailbox_state
