#pragma once

#include <cstdint>
#include <string_view>

namespace refract::ge {

enum class Command : std::uint8_t {
  nop = 0x00,
  vertex_address = 0x01,
  index_address = 0x02,
  primitive = 0x04,
  bezier = 0x05,
  spline = 0x06,
  bounding_box = 0x07,
  jump = 0x08,
  conditional_jump = 0x09,
  call = 0x0a,
  ret = 0x0b,
  end = 0x0c,
  signal = 0x0e,
  finish = 0x0f,
  base = 0x10,
  vertex_type = 0x12,
  offset_address = 0x13,
  origin = 0x14,
  bone_matrix_number = 0x2a,
  bone_matrix_data = 0x2b,
  world_matrix_number = 0x3a,
  world_matrix_data = 0x3b,
  view_matrix_number = 0x3c,
  view_matrix_data = 0x3d,
  projection_matrix_number = 0x3e,
  projection_matrix_data = 0x3f,
  load_clut = 0xc4,
  transfer_start = 0xea,
};

enum class CommandFlow : std::uint8_t {
  linear,
  draw,
  jump,
  call,
  ret,
  signal,
  finish,
  end,
  transfer,
};

struct CommandMetadata {
  Command command{};
  std::string_view name{"unknown"};
  CommandFlow flow{CommandFlow::linear};
};

constexpr CommandMetadata command_metadata(std::uint8_t opcode) {
  switch (static_cast<Command>(opcode)) {
  case Command::nop: return {Command::nop, "NOP", CommandFlow::linear};
  case Command::vertex_address:
    return {Command::vertex_address, "VADDR", CommandFlow::linear};
  case Command::index_address:
    return {Command::index_address, "IADDR", CommandFlow::linear};
  case Command::primitive:
    return {Command::primitive, "PRIM", CommandFlow::draw};
  case Command::bezier:
    return {Command::bezier, "BEZIER", CommandFlow::draw};
  case Command::spline:
    return {Command::spline, "SPLINE", CommandFlow::draw};
  case Command::bounding_box:
    return {Command::bounding_box, "BOUNDINGBOX", CommandFlow::draw};
  case Command::jump: return {Command::jump, "JUMP", CommandFlow::jump};
  case Command::conditional_jump:
    return {Command::conditional_jump, "BJUMP", CommandFlow::jump};
  case Command::call: return {Command::call, "CALL", CommandFlow::call};
  case Command::ret: return {Command::ret, "RET", CommandFlow::ret};
  case Command::end: return {Command::end, "END", CommandFlow::end};
  case Command::signal:
    return {Command::signal, "SIGNAL", CommandFlow::signal};
  case Command::finish:
    return {Command::finish, "FINISH", CommandFlow::finish};
  case Command::base: return {Command::base, "BASE", CommandFlow::linear};
  case Command::vertex_type:
    return {Command::vertex_type, "VTYPE", CommandFlow::linear};
  case Command::offset_address:
    return {Command::offset_address, "OFFSETADDR", CommandFlow::linear};
  case Command::origin:
    return {Command::origin, "ORIGIN", CommandFlow::linear};
  case Command::bone_matrix_number:
    return {Command::bone_matrix_number, "BONEMATRIXNUMBER",
            CommandFlow::linear};
  case Command::bone_matrix_data:
    return {Command::bone_matrix_data, "BONEMATRIXDATA", CommandFlow::linear};
  case Command::world_matrix_number:
    return {Command::world_matrix_number, "WORLDMATRIXNUMBER",
            CommandFlow::linear};
  case Command::world_matrix_data:
    return {Command::world_matrix_data, "WORLDMATRIXDATA",
            CommandFlow::linear};
  case Command::view_matrix_number:
    return {Command::view_matrix_number, "VIEWMATRIXNUMBER",
            CommandFlow::linear};
  case Command::view_matrix_data:
    return {Command::view_matrix_data, "VIEWMATRIXDATA", CommandFlow::linear};
  case Command::projection_matrix_number:
    return {Command::projection_matrix_number, "PROJMATRIXNUMBER",
            CommandFlow::linear};
  case Command::projection_matrix_data:
    return {Command::projection_matrix_data, "PROJMATRIXDATA",
            CommandFlow::linear};
  case Command::load_clut:
    return {Command::load_clut, "LOADCLUT", CommandFlow::transfer};
  case Command::transfer_start:
    return {Command::transfer_start, "TRANSFERSTART", CommandFlow::transfer};
  }
  return {static_cast<Command>(opcode), "unknown", CommandFlow::linear};
}

constexpr std::uint8_t opcode(std::uint32_t instruction) {
  return static_cast<std::uint8_t>(instruction >> 24U);
}

constexpr std::uint32_t argument(std::uint32_t instruction) {
  return instruction & 0x00ffffffU;
}

} // namespace refract::ge
