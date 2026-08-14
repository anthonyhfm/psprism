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
  texture_matrix_number = 0x40,
  texture_matrix_data = 0x41,
  framebuffer_address = 0x9c,
  framebuffer_width = 0x9d,
  depthbuffer_address = 0x9e,
  depthbuffer_width = 0x9f,
  texture_address_0 = 0xa0,
  texture_buffer_width_0 = 0xa8,
  clut_address = 0xb0,
  clut_address_upper = 0xb1,
  texture_size_0 = 0xb8,
  texture_mode = 0xc2,
  texture_format = 0xc3,
  load_clut = 0xc4,
  clut_format = 0xc5,
  texture_filter = 0xc6,
  texture_wrap = 0xc7,
  texture_level = 0xc8,
  texture_function = 0xc9,
  framebuffer_format = 0xd2,
  clear_mode = 0xd3,
  stencil_test = 0xdc,
  stencil_operation = 0xdd,
  color_write_mask = 0xe8,
  alpha_write_mask = 0xe9,
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
  if (opcode >= 0xa0U && opcode <= 0xa7U)
    return {static_cast<Command>(opcode), "TEXADDR", CommandFlow::linear};
  if (opcode >= 0xa8U && opcode <= 0xafU)
    return {static_cast<Command>(opcode), "TEXBUFWIDTH",
            CommandFlow::linear};
  if (opcode >= 0xb8U && opcode <= 0xbfU)
    return {static_cast<Command>(opcode), "TEXSIZE", CommandFlow::linear};
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
  case Command::texture_matrix_number:
    return {Command::texture_matrix_number, "TGENMATRIXNUMBER",
            CommandFlow::linear};
  case Command::texture_matrix_data:
    return {Command::texture_matrix_data, "TGENMATRIXDATA",
            CommandFlow::linear};
  case Command::framebuffer_address:
    return {Command::framebuffer_address, "FRAMEBUFPTR", CommandFlow::linear};
  case Command::framebuffer_width:
    return {Command::framebuffer_width, "FRAMEBUFWIDTH", CommandFlow::linear};
  case Command::depthbuffer_address:
    return {Command::depthbuffer_address, "ZBUFPTR", CommandFlow::linear};
  case Command::depthbuffer_width:
    return {Command::depthbuffer_width, "ZBUFWIDTH", CommandFlow::linear};
  case Command::clut_address:
    return {Command::clut_address, "CLUTADDR", CommandFlow::linear};
  case Command::clut_address_upper:
    return {Command::clut_address_upper, "CLUTADDRUPPER",
            CommandFlow::linear};
  case Command::texture_mode:
    return {Command::texture_mode, "TEXMODE", CommandFlow::linear};
  case Command::texture_format:
    return {Command::texture_format, "TEXFORMAT", CommandFlow::linear};
  case Command::load_clut:
    return {Command::load_clut, "LOADCLUT", CommandFlow::transfer};
  case Command::clut_format:
    return {Command::clut_format, "CLUTFORMAT", CommandFlow::linear};
  case Command::texture_filter:
    return {Command::texture_filter, "TEXFILTER", CommandFlow::linear};
  case Command::texture_wrap:
    return {Command::texture_wrap, "TEXWRAP", CommandFlow::linear};
  case Command::texture_level:
    return {Command::texture_level, "TEXLEVEL", CommandFlow::linear};
  case Command::texture_function:
    return {Command::texture_function, "TEXFUNC", CommandFlow::linear};
  case Command::framebuffer_format:
    return {Command::framebuffer_format, "FRAMEBUFPIXFORMAT",
            CommandFlow::linear};
  case Command::clear_mode:
    return {Command::clear_mode, "CLEARMODE", CommandFlow::linear};
  case Command::stencil_test:
    return {Command::stencil_test, "STENCILTEST", CommandFlow::linear};
  case Command::stencil_operation:
    return {Command::stencil_operation, "STENCILOP", CommandFlow::linear};
  case Command::color_write_mask:
    return {Command::color_write_mask, "MASKRGB", CommandFlow::linear};
  case Command::alpha_write_mask:
    return {Command::alpha_write_mask, "MASKALPHA", CommandFlow::linear};
  case Command::transfer_start:
    return {Command::transfer_start, "TRANSFERSTART", CommandFlow::transfer};
  case Command::texture_address_0:
  case Command::texture_buffer_width_0:
  case Command::texture_size_0: break;
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
