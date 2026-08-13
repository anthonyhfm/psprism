#pragma once

#include "ge_command.hpp"
#include "ge_state.hpp"

#include <cstdint>

namespace refract::ge {

struct DecodedCommand {
  std::uint32_t program_counter{};
  std::uint32_t instruction{};
  std::uint8_t opcode{};
  std::uint32_t argument{};
  CommandMetadata metadata{};
};

class CommandDecoder {
public:
  static constexpr DecodedCommand decode(std::uint32_t program_counter,
                                         std::uint32_t instruction) {
    const auto decoded_opcode = ge::opcode(instruction);
    return {program_counter, instruction, decoded_opcode,
            ge::argument(instruction), command_metadata(decoded_opcode)};
  }
};

// All commands enter through this small front door.  State-specific handlers
// remain separable from decoding and render submission while the legacy
// renderer is migrated incrementally.
class Interpreter {
public:
  static DecodedCommand accept(State& state, std::uint32_t list_id,
                               std::uint32_t program_counter,
                               std::uint32_t instruction) {
    const auto decoded = CommandDecoder::decode(program_counter, instruction);
    state.commands[decoded.opcode] = instruction;
    state.trace.record(list_id, program_counter, instruction);
    return decoded;
  }
};

} // namespace refract::ge
