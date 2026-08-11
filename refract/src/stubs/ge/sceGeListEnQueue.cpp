void sceGeListEnQueue(Implementation& implementation, psprecomp::State& state) {
#if !defined(__PSP__)
  static std::atomic<std::uint32_t> next_list{1};
  const auto submission = implementation.submitted_ge_lists++;
  struct PendingCallback {
    std::uint32_t entry;
    std::uint32_t command_argument;
    std::uint32_t common_argument;
  };
  std::vector<PendingCallback> pending_callbacks;
  {
    std::lock_guard graphics_lock(implementation.graphics.mutex);
    auto& graphics = implementation.graphics;
    host::begin_ge_frame();
    std::array<std::uint32_t, 256> commands{};
    struct GeCallFrame {
      std::uint32_t return_address;
      std::uint32_t offset_address;
    };
    std::vector<GeCallFrame> call_stack;
    call_stack.reserve(64U);
    std::unordered_map<TextureKey, DecodedTexture, TextureKeyHash>
        texture_cache;
    texture_cache.reserve(64U);
    std::vector<std::uint32_t> vertex_indices;
    vertex_indices.reserve(4096U);
    auto program_counter = state.gpr[4];
    std::uint32_t words{};
    std::uint32_t primitives{};
    std::uint32_t submitted_primitives{};
    std::uint32_t invalid_layouts{};
    std::uint32_t invalid_indices{};
    std::uint32_t invalid_vertices{};
    std::uint32_t last_render_target{};
    bool ended{};
    // CALLed display-list fragments count toward this guard as well. Large
    // games can legitimately execute well beyond 64K words before END.
    constexpr std::uint32_t max_ge_command_words = 1U << 20U;
    for (; words < max_ge_command_words; ++words) {
      const auto* pointer =
          psprecomp::mapped_address(state, program_counter, 4U);
      if (pointer == nullptr)
        break;
      std::uint32_t instruction{};
      std::memcpy(&instruction, pointer, sizeof(instruction));
      const auto command = instruction >> 24U;
      const auto argument = instruction & 0x00ffffffU;
      const auto next = program_counter + 4U;
      ++commands[command];
      graphics.commands[command] = instruction;
      const auto relative_address = [&](std::uint32_t value) {
        const auto base = (graphics.commands[0x10U] & 0x000f0000U) << 8U;
        return (graphics.offset_address + (base | value)) & 0x0fffffffU;
      };
      if (command == 0x01U) {
        graphics.vertex_address = relative_address(argument);
      } else if (command == 0x02U) {
        graphics.index_address = relative_address(argument);
      } else if (command == 0x3aU) {
        graphics.world_matrix_index = argument & 0xfU;
      } else if (command == 0x3bU) {
        if (graphics.world_matrix_index < graphics.world_matrix.size())
          graphics.world_matrix[graphics.world_matrix_index++] =
              float24(argument);
      } else if (command == 0x3cU) {
        graphics.view_matrix_index = argument & 0xfU;
      } else if (command == 0x3dU) {
        if (graphics.view_matrix_index < graphics.view_matrix.size())
          graphics.view_matrix[graphics.view_matrix_index++] =
              float24(argument);
      } else if (command == 0x3eU) {
        graphics.projection_matrix_index = argument & 0xfU;
      } else if (command == 0x3fU) {
        if (graphics.projection_matrix_index <
            graphics.projection_matrix.size())
          graphics.projection_matrix[graphics.projection_matrix_index++] =
              float24(argument);
      } else if (command == 0x2aU) {
        graphics.bone_matrix_index = argument & 0x7fU;
      } else if (command == 0x2bU) {
        if (graphics.bone_matrix_index < graphics.bone_matrices.size())
          graphics.bone_matrices[graphics.bone_matrix_index++] =
              float24(argument);
      } else if (command == 0xc4U) {
        // LOADCLUT copies palette data into GE-internal storage.  Later
        // texture draws keep using that copy even when the source address
        // changes, so decoding directly from CLUTADDR is not equivalent.
        const auto clut_address =
            (graphics.commands[0xb0U] & 0x00fffff0U) |
            ((graphics.commands[0xb1U] << 8U) & 0x0f000000U);
        const auto clut_bytes =
            std::min<std::size_t>((argument & 0x3fU) * 32U,
                                  graphics.clut.size());
        if (const auto* source =
                psprecomp::mapped_address(state, clut_address, clut_bytes)) {
          if (!std::equal(source, source + clut_bytes,
                          graphics.clut.begin())) {
            std::copy_n(source, clut_bytes, graphics.clut.begin());
            texture_cache.clear();
          }
        }
      } else if (command == 0x04U) {
        const auto primitive_type = (argument >> 16U) & 7U;
        const auto vertex_count = argument & 0xffffU;
        const auto vertex_type = graphics.commands[0x12U] & 0x00ffffffU;
        if (implementation.verbose && submission < 2U &&
            primitives < 32U) {
          std::fprintf(
              stderr,
              "[psprism:ge] prim=%u type=%u count=%u vtype=%06x "
              "vaddr=%08x iaddr=%08x tex=%u texaddr=%06x "
              "texbuf=%06x texsize=%06x texfmt=%u texmode=%06x "
              "depth=%u/%u/%u blend=%u/%03x clear=%06x "
              "cull=%u/%u texfunc=%06x wrap=%06x "
              "color=%u/%u/%06x/%06x alpha=%u/%u/%02x/%02x\n",
              primitives, primitive_type, vertex_count, vertex_type,
              graphics.vertex_address, graphics.index_address,
              graphics.commands[0x1eU] & 1U,
              graphics.commands[0xa0U] & 0x00ffffffU,
              graphics.commands[0xa8U] & 0x00ffffffU,
              graphics.commands[0xb8U] & 0x00ffffffU,
              graphics.commands[0xc3U] & 0xfU,
              graphics.commands[0xc2U] & 0x00ffffffU,
              graphics.commands[0x23U] & 1U,
              (graphics.commands[0xe7U] & 1U) == 0,
              graphics.commands[0xdeU] & 7U, graphics.commands[0x21U] & 1U,
              graphics.commands[0xdfU] & 0xfffU,
              graphics.commands[0xd3U] & 0x00ffffffU,
              graphics.commands[0x1dU] & 1U, graphics.commands[0x9bU] & 1U,
              graphics.commands[0xc9U] & 0x00ffffffU,
              graphics.commands[0xc7U] & 0x00ffffffU,
              graphics.commands[0x27U] & 1U,
              graphics.commands[0xd8U] & 3U,
              graphics.commands[0xd9U] & 0x00ffffffU,
              graphics.commands[0xdaU] & 0x00ffffffU,
              graphics.commands[0x22U] & 1U,
              graphics.commands[0xdbU] & 7U,
              (graphics.commands[0xdbU] >> 8U) & 0xffU,
              (graphics.commands[0xdbU] >> 16U) & 0xffU);
        }
        const auto layout = vertex_layout(vertex_type);
        const auto index_type = (vertex_type >> 11U) & 3U;
        const auto framebuffer_stride =
            graphics.commands[0x9dU] & 0x7fcU;
        const auto scissor_width =
            (graphics.commands[0xd5U] & 0x3ffU) + 1U;
        const auto scissor_height =
            ((graphics.commands[0xd5U] >> 10U) & 0x3ffU) + 1U;
        const auto render_target_width = std::clamp(
            std::max(framebuffer_stride, scissor_width), 1U, 1024U);
        const auto render_target_height =
            std::clamp(scissor_height, 1U, 1024U);
        if (layout.stride != 0 && primitive_type <= 6U) {
          const auto index_size = component_size(index_type);
          const auto index_byte_count =
              static_cast<std::size_t>(vertex_count) * index_size;
          vertex_indices.resize(vertex_count);
          bool indices_valid = true;
          std::uint32_t maximum_index{};
          if (index_type == 0) {
            for (std::uint32_t index = 0; index < vertex_count; ++index)
              vertex_indices[index] = index;
            maximum_index = vertex_count == 0 ? 0U : vertex_count - 1U;
          } else if (const auto* indices = psprecomp::mapped_address(
                         state, graphics.index_address, index_byte_count)) {
            for (std::uint32_t index = 0; index < vertex_count; ++index) {
              std::uint32_t decoded_index{};
              if (index_type == 1U) {
                decoded_index = indices[index];
              } else if (index_type == 2U) {
                std::uint16_t value{};
                std::memcpy(&value, indices + index * 2U, sizeof(value));
                decoded_index = value;
              } else {
                std::memcpy(&decoded_index, indices + index * 4U,
                            sizeof(decoded_index));
              }
              vertex_indices[index] = decoded_index;
              maximum_index = std::max(maximum_index, decoded_index);
            }
          } else {
            indices_valid = false;
          }
          const auto vertex_byte_count =
              (static_cast<std::size_t>(maximum_index) + 1U) * layout.stride;
          if (indices_valid) {
            if (const auto* source = psprecomp::mapped_address(
                    state, graphics.vertex_address, vertex_byte_count)) {
              std::vector<host::GeometryVertex> vertices(vertex_count);
              const TextureKey texture_key{
                  graphics.commands[0x1eU], graphics.commands[0xa0U],
                  graphics.commands[0xa8U], graphics.commands[0xb8U],
                  graphics.commands[0xc2U], graphics.commands[0xc3U],
                  graphics.commands[0xc5U]};
              auto cached_texture = texture_cache.find(texture_key);
              if (cached_texture == texture_cache.end()) {
                cached_texture = texture_cache
                                     .emplace(texture_key,
                                              decode_texture(
                                                  state, graphics.commands,
                                                  graphics.clut))
                                     .first;
              }
              auto texture = cached_texture->second;
              const auto material_color =
                  (graphics.commands[0x55U] & 0x00ffffffU) |
                  ((graphics.commands[0x58U] & 0xffU) << 24U);
              for (std::uint32_t index = 0; index < vertex_count; ++index) {
                const auto* input =
                    source + vertex_indices[index] * layout.stride;
                const auto* position = input + layout.position_offset;
                float decoded[3]{};
                const auto through = (vertex_type & (1U << 23U)) != 0;
                if (layout.position_type == 1U) {
                  for (std::size_t component = 0; component < 3U; ++component)
                    decoded[component] =
                        through ? static_cast<float>(position[component])
                                : static_cast<float>(
                                      reinterpret_cast<const std::int8_t*>(
                                          position)[component]) /
                                      127.0F;
                } else if (layout.position_type == 2U) {
                  for (std::size_t component = 0; component < 3U;
                       ++component) {
                    if (through) {
                      std::uint16_t value{};
                      std::memcpy(&value, position + component * 2U,
                                  sizeof(value));
                      decoded[component] = static_cast<float>(value);
                    } else {
                      std::int16_t value{};
                      std::memcpy(&value, position + component * 2U,
                                  sizeof(value));
                      decoded[component] =
                          static_cast<float>(value) / 32767.0F;
                    }
                  }
                } else if (layout.position_type == 3U) {
                  std::memcpy(decoded, position, sizeof(decoded));
                }
                std::array<float, 12> skin_matrix{};
                if (!through && layout.weight_type != 0U) {
                  const auto* weights = input + layout.weight_offset;
                  for (std::uint32_t bone = 0; bone < layout.weight_count;
                       ++bone) {
                    float weight{};
                    if (layout.weight_type == 1U) {
                      weight = static_cast<float>(weights[bone]) / 128.0F;
                    } else if (layout.weight_type == 2U) {
                      std::uint16_t packed{};
                      std::memcpy(&packed, weights + bone * 2U,
                                  sizeof(packed));
                      weight = static_cast<float>(packed) / 32768.0F;
                    } else {
                      std::memcpy(&weight, weights + bone * 4U,
                                  sizeof(weight));
                    }
                    for (std::size_t element = 0;
                         element < skin_matrix.size(); ++element) {
                      skin_matrix[element] +=
                          weight * graphics.bone_matrices[bone * 12U +
                                                         element];
                    }
                  }
                  float skinned[3]{};
                  transform43(skin_matrix, decoded, skinned);
                  std::copy(std::begin(skinned), std::end(skinned), decoded);
                }
                auto& output = vertices[index];
                float world_position[3]{};
                if (through) {
                  output.position[0] =
                      decoded[0] /
                          (static_cast<float>(render_target_width) * 0.5F) -
                      1.0F;
                  output.position[1] =
                      1.0F -
                      decoded[1] /
                          (static_cast<float>(render_target_height) * 0.5F);
                  output.position[2] = decoded[2] / 65535.0F;
                  output.position[3] = 1.0F;
                } else {
                  float view[3]{};
                  transform43(graphics.world_matrix, decoded,
                              world_position);
                  transform43(graphics.view_matrix, world_position, view);
                  transform44(graphics.projection_matrix, view,
                              output.position);
                  const auto clip_w = output.position[3];
                  if (clip_w != 0.0F) {
                    const auto screen_x =
                        output.position[0] / clip_w *
                            float24(graphics.commands[0x42U]) +
                        float24(graphics.commands[0x45U]) -
                        static_cast<float>(graphics.commands[0x4cU] &
                                           0xffffU) /
                            16.0F;
                    const auto screen_y =
                        output.position[1] / clip_w *
                            float24(graphics.commands[0x43U]) +
                        float24(graphics.commands[0x46U]) -
                        static_cast<float>(graphics.commands[0x4dU] &
                                           0xffffU) /
                            16.0F;
                    const auto screen_z =
                        output.position[2] / clip_w *
                            float24(graphics.commands[0x44U]) +
                        float24(graphics.commands[0x47U]);
                    output.position[0] =
                        (screen_x /
                             (static_cast<float>(render_target_width) *
                              0.5F) -
                         1.0F) *
                        clip_w;
                    output.position[1] =
                        (1.0F -
                         screen_y /
                             (static_cast<float>(render_target_height) *
                              0.5F)) *
                        clip_w;
                    output.position[2] = screen_z / 65535.0F * clip_w;
                  }
                }
                float world_normal[3]{0.0F, 0.0F, 1.0F};
                if (!through && layout.normal_type != 0U) {
                  const auto* normal = input + layout.normal_offset;
                  float decoded_normal[3]{};
                  if (layout.normal_type == 1U) {
                    for (std::size_t component = 0; component < 3U;
                         ++component) {
                      decoded_normal[component] =
                          static_cast<float>(
                              reinterpret_cast<const std::int8_t*>(normal)
                                  [component]) /
                          128.0F;
                    }
                  } else if (layout.normal_type == 2U) {
                    for (std::size_t component = 0; component < 3U;
                         ++component) {
                      std::int16_t value{};
                      std::memcpy(&value, normal + component * 2U,
                                  sizeof(value));
                      decoded_normal[component] =
                          static_cast<float>(value) / 32768.0F;
                    }
                  } else {
                    std::memcpy(decoded_normal, normal,
                                sizeof(decoded_normal));
                  }
                  if (layout.weight_type != 0U) {
                    float skinned_normal[3]{};
                    transform_normal43(skin_matrix, decoded_normal,
                                       skinned_normal);
                    std::copy(std::begin(skinned_normal),
                              std::end(skinned_normal), decoded_normal);
                  }
                  if ((graphics.commands[0x51U] & 1U) != 0) {
                    for (auto& component : decoded_normal)
                      component = -component;
                  }
                  transform_normal43(graphics.world_matrix, decoded_normal,
                                     world_normal);
                  normalize3(world_normal);
                }
                std::uint32_t color = material_color;
                if (layout.color_type == 7U) {
                  std::memcpy(&color, input + layout.color_offset,
                              sizeof(color));
                } else if (layout.color_type >= 4U) {
                  std::uint16_t packed{};
                  std::memcpy(&packed, input + layout.color_offset,
                              sizeof(packed));
                  if (layout.color_type == 4U) {
                    color = ((packed & 31U) << 3U) |
                            (((packed >> 5U) & 63U) * 255U / 63U << 8U) |
                            (((packed >> 11U) & 31U) << 19U) | 0xff000000U;
                  } else if (layout.color_type == 5U) {
                    color = ((packed & 31U) << 3U) |
                            (((packed >> 5U) & 31U) << 11U) |
                            (((packed >> 10U) & 31U) << 19U) |
                            ((packed & 0x8000U) != 0 ? 0xff000000U : 0U);
                  } else {
                    color = ((packed & 15U) * 17U) |
                            (((packed >> 4U) & 15U) * 17U << 8U) |
                            (((packed >> 8U) & 15U) * 17U << 16U) |
                            (((packed >> 12U) & 15U) * 17U << 24U);
                  }
                }
                output.color[0] = static_cast<float>(color & 0xffU) / 255.0F;
                output.color[1] =
                    static_cast<float>((color >> 8U) & 0xffU) / 255.0F;
                output.color[2] =
                    static_cast<float>((color >> 16U) & 0xffU) / 255.0F;
                output.color[3] =
                    static_cast<float>((color >> 24U) & 0xffU) / 255.0F;
                if (!through && (graphics.commands[0x17U] & 1U) != 0) {
                  const auto channel = [](std::uint32_t packed,
                                          std::size_t component) {
                    return static_cast<float>((packed >> (component * 8U)) &
                                              0xffU) /
                           255.0F;
                  };
                  const auto material_update =
                      layout.color_type != 0U
                          ? graphics.commands[0x53U] & 7U
                          : 0U;
                  float ambient[4]{};
                  float diffuse[3]{};
                  for (std::size_t component = 0; component < 3U;
                       ++component) {
                    ambient[component] =
                        (material_update & 1U) != 0
                            ? output.color[component]
                            : channel(graphics.commands[0x55U], component);
                    diffuse[component] =
                        (material_update & 2U) != 0
                            ? output.color[component]
                            : channel(graphics.commands[0x56U], component);
                    output.color[component] =
                        channel(graphics.commands[0x5cU], component) *
                            ambient[component] +
                        channel(graphics.commands[0x54U], component);
                  }
                  ambient[3] = (material_update & 1U) != 0
                                   ? output.color[3]
                                   : channel(graphics.commands[0x58U], 0U);
                  output.color[3] =
                      channel(graphics.commands[0x5dU], 0U) * ambient[3];
                  for (std::uint32_t light = 0; light < 4U; ++light) {
                    if ((graphics.commands[0x18U + light] & 1U) == 0)
                      continue;
                    const auto light_type =
                        (graphics.commands[0x5fU + light] >> 8U) & 3U;
                    float to_light[3]{};
                    for (std::size_t component = 0; component < 3U;
                         ++component) {
                      to_light[component] =
                          float24(graphics.commands[0x63U + light * 3U +
                                                     component]);
                      if (light_type != 0U)
                        to_light[component] -= world_position[component];
                    }
                    const auto distance =
                        std::sqrt(to_light[0] * to_light[0] +
                                  to_light[1] * to_light[1] +
                                  to_light[2] * to_light[2]);
                    if (distance > 0.0F) {
                      to_light[0] /= distance;
                      to_light[1] /= distance;
                      to_light[2] /= distance;
                    }
                    const auto incidence = std::max(
                        0.0F, to_light[0] * world_normal[0] +
                                  to_light[1] * world_normal[1] +
                                  to_light[2] * world_normal[2]);
                    float scale = 1.0F;
                    if (light_type != 0U) {
                      const auto constant = float24(
                          graphics.commands[0x7bU + light * 3U]);
                      const auto linear = float24(
                          graphics.commands[0x7cU + light * 3U]);
                      const auto quadratic = float24(
                          graphics.commands[0x7dU + light * 3U]);
                      const auto denominator =
                          constant + linear * distance +
                          quadratic * distance * distance;
                      scale = denominator > 0.0F
                                  ? std::clamp(1.0F / denominator, 0.0F, 1.0F)
                                  : 0.0F;
                    }
                    for (std::size_t component = 0; component < 3U;
                         ++component) {
                      const auto light_ambient = channel(
                          graphics.commands[0x8fU + light * 3U], component);
                      const auto light_diffuse = channel(
                          graphics.commands[0x90U + light * 3U], component);
                      output.color[component] +=
                          (light_ambient * ambient[component] +
                           light_diffuse * diffuse[component] * incidence) *
                          scale;
                      output.color[component] =
                          std::clamp(output.color[component], 0.0F, 1.0F);
                    }
                  }
                }
                if (layout.texture_type != 0 && texture.width != 0 &&
                    texture.height != 0) {
                  float u{};
                  float v{};
                  if (!through &&
                      (graphics.commands[0xc0U] & 3U) == 2U) {
                    const auto shade_coordinate = [&](std::uint32_t light) {
                      float light_position[3]{};
                      for (std::size_t component = 0; component < 3U;
                           ++component) {
                        light_position[component] = float24(
                            graphics.commands[0x63U + light * 3U +
                                              component]);
                      }
                      const auto length =
                          std::sqrt(light_position[0] * light_position[0] +
                                    light_position[1] * light_position[1] +
                                    light_position[2] * light_position[2]);
                      float factor = world_normal[2];
                      if (length > 0.0F) {
                        factor = (light_position[0] * world_normal[0] +
                                  light_position[1] * world_normal[1] +
                                  light_position[2] * world_normal[2]) /
                                 length;
                      }
                      return (1.0F + factor) * 0.5F;
                    };
                    u = shade_coordinate(graphics.commands[0xc1U] & 3U) *
                        float24(graphics.commands[0x48U]);
                    v = shade_coordinate(
                            (graphics.commands[0xc1U] >> 8U) & 3U) *
                        float24(graphics.commands[0x49U]);
                  } else {
                    const auto* coordinates =
                        input + layout.texture_offset;
                    if (layout.texture_type == 1U) {
                      u = static_cast<float>(coordinates[0]);
                      v = static_cast<float>(coordinates[1]);
                      if (!through) {
                        u /= 128.0F;
                        v /= 128.0F;
                      }
                    } else if (layout.texture_type == 2U) {
                      std::uint16_t packed_u{};
                      std::uint16_t packed_v{};
                      std::memcpy(&packed_u, coordinates, sizeof(packed_u));
                      std::memcpy(&packed_v, coordinates + 2U,
                                  sizeof(packed_v));
                      u = static_cast<float>(packed_u);
                      v = static_cast<float>(packed_v);
                      if (!through) {
                        u /= 32768.0F;
                        v /= 32768.0F;
                      }
                    } else {
                      std::memcpy(&u, coordinates, sizeof(u));
                      std::memcpy(&v, coordinates + 4U, sizeof(v));
                    }
                    if (through) {
                      u /= static_cast<float>(texture.width);
                      v /= static_cast<float>(texture.height);
                    } else {
                      u = u * float24(graphics.commands[0x48U]) +
                          float24(graphics.commands[0x4aU]);
                      v = v * float24(graphics.commands[0x49U]) +
                          float24(graphics.commands[0x4bU]);
                    }
                  }
                  output.texture[0] = u;
                  output.texture[1] = v;
                }
              }
              host::GeometryState render_state;
              render_state.render_target_address =
                  0x04000000U |
                  (graphics.commands[0x9cU] & 0x001ffff0U);
              last_render_target = render_state.render_target_address;
              render_state.render_target_width = render_target_width;
              render_state.render_target_height = render_target_height;
              render_state.texture_address = texture.address;
              const auto clear_mode =
                  (graphics.commands[0xd3U] & 1U) != 0;
              if (clear_mode) {
                // PSP clear-mode draws ignore the currently bound texture and
                // fixed-function tests.  Letting those states leak into the
                // clear rectangle leaves stale color/depth targets behind.
                texture = {};
                render_state.texture_address = 0U;
              }
              render_state.cull_face =
                  !clear_mode && (graphics.commands[0x1dU] & 1U) != 0;
              render_state.front_face_clockwise =
                  (graphics.commands[0x9bU] & 1U) != 0;
              render_state.depth_test =
                  !clear_mode && (graphics.commands[0x23U] & 1U) != 0;
              render_state.depth_write =
                  clear_mode
                      ? (graphics.commands[0xd3U] & 0x400U) != 0
                      : (graphics.commands[0xe7U] & 1U) == 0;
              render_state.depth_function = graphics.commands[0xdeU] & 7U;
              render_state.alpha_blend =
                  !clear_mode && (graphics.commands[0x21U] & 1U) != 0;
              render_state.blend_source = graphics.commands[0xdfU] & 0xfU;
              render_state.blend_destination =
                  (graphics.commands[0xdfU] >> 4U) & 0xfU;
              render_state.blend_equation =
                  (graphics.commands[0xdfU] >> 8U) & 7U;
              render_state.blend_fix_a =
                  graphics.commands[0xe0U] & 0x00ffffffU;
              render_state.blend_fix_b =
                  graphics.commands[0xe1U] & 0x00ffffffU;
              render_state.color_test =
                  !clear_mode && (graphics.commands[0x27U] & 1U) != 0;
              render_state.color_function = graphics.commands[0xd8U] & 3U;
              render_state.color_reference =
                  graphics.commands[0xd9U] & 0x00ffffffU;
              render_state.color_mask =
                  graphics.commands[0xdaU] & 0x00ffffffU;
              render_state.alpha_test =
                  !clear_mode && (graphics.commands[0x22U] & 1U) != 0;
              render_state.alpha_function = graphics.commands[0xdbU] & 7U;
              render_state.alpha_reference =
                  (graphics.commands[0xdbU] >> 8U) & 0xffU;
              render_state.alpha_mask =
                  (graphics.commands[0xdbU] >> 16U) & 0xffU;
              render_state.texture_clamp_s =
                  (graphics.commands[0xc7U] & 1U) != 0;
              render_state.texture_clamp_t =
                  (graphics.commands[0xc7U] & 0x100U) != 0;
              render_state.texture_linear_filter =
                  (graphics.commands[0xc6U] & 1U) != 0 ||
                  (graphics.commands[0xc6U] & 0x100U) != 0;
              render_state.texture_function = graphics.commands[0xc9U] & 7U;
              render_state.texture_alpha_used =
                  (graphics.commands[0xc9U] & 0x100U) != 0;
              render_state.texture_color_double =
                  host::texture_color_doubling_enabled(
                      graphics.commands[0xc9U]);
              render_state.texture_environment_color =
                  graphics.commands[0xcaU] & 0x00ffffffU;
              auto submitted_type = primitive_type;
              if (primitive_type == 5U && vertices.size() >= 3U) {
                std::vector<host::GeometryVertex> triangles;
                triangles.reserve((vertices.size() - 2U) * 3U);
                for (std::size_t fan = 1U; fan + 1U < vertices.size();
                     ++fan) {
                  triangles.push_back(vertices[0]);
                  triangles.push_back(vertices[fan]);
                  triangles.push_back(vertices[fan + 1U]);
                }
                vertices = std::move(triangles);
                submitted_type = 3U;
              } else if (primitive_type == 6U && vertices.size() >= 2U) {
                std::vector<host::GeometryVertex> triangles;
                triangles.reserve((vertices.size() / 2U) * 6U);
                for (std::size_t rectangle = 0U;
                     rectangle + 1U < vertices.size(); rectangle += 2U) {
                  const auto& source_top_left = vertices[rectangle];
                  const auto& source_bottom_right = vertices[rectangle + 1U];
                  const auto ndc = [](const host::GeometryVertex& vertex,
                                      std::size_t component) {
                    return vertex.position[3] != 0.0F
                               ? vertex.position[component] /
                                     vertex.position[3]
                               : vertex.position[component];
                  };
                  const auto left = ndc(source_top_left, 0U);
                  const auto top = ndc(source_top_left, 1U);
                  const auto right = ndc(source_bottom_right, 0U);
                  const auto bottom = ndc(source_bottom_right, 1U);
  
                  // PSP sprites take color, depth and W from the second
                  // vertex.  Only the screen-space corner and UV differ.
                  auto bottom_right = source_bottom_right;
                  auto top_right = source_bottom_right;
                  auto top_left = source_bottom_right;
                  auto bottom_left = source_bottom_right;
                  top_right.position[1] = top * top_right.position[3];
                  top_left.position[0] = left * top_left.position[3];
                  top_left.position[1] = top * top_left.position[3];
                  bottom_left.position[0] = left * bottom_left.position[3];
                  top_right.texture[1] = source_top_left.texture[1];
                  top_left.texture[0] = source_top_left.texture[0];
                  top_left.texture[1] = source_top_left.texture[1];
                  bottom_left.texture[0] = source_top_left.texture[0];
  
                  // The GE rotates sprite UVs when the two supplied
                  // corners describe opposing axes.  Metal's Y axis is
                  // inverted relative to PSP screen coordinates, hence
                  // the same-direction comparison in NDC space.
                  if ((left < right && top < bottom) ||
                      (left > right && top > bottom)) {
                    std::swap(top_right.texture[0], bottom_left.texture[0]);
                    std::swap(top_right.texture[1], bottom_left.texture[1]);
                  }
  
                  triangles.push_back(bottom_right);
                  triangles.push_back(top_right);
                  triangles.push_back(top_left);
                  triangles.push_back(bottom_left);
                  triangles.push_back(bottom_right);
                  triangles.push_back(top_left);
                }
                vertices = std::move(triangles);
                submitted_type = 3U;
                render_state.cull_face = false;
              }
              host::submit_ge_primitive(submitted_type, std::move(vertices),
                                        std::move(texture.pixels),
                                        texture.width, texture.height,
                                        render_state);
              ++submitted_primitives;
            } else {
              ++invalid_vertices;
            }
          } else {
            ++invalid_indices;
          }
          if (index_type == 0)
            graphics.vertex_address += static_cast<std::uint32_t>(
                static_cast<std::size_t>(vertex_count) * layout.stride);
          else
            graphics.index_address +=
                static_cast<std::uint32_t>(index_byte_count);
        } else {
          ++invalid_layouts;
        }
        ++primitives;
      } else if (command == 0x08U) {
        program_counter = relative_address(argument & 0x00fffffcU);
        continue;
      } else if (command == 0x0aU) {
        if (call_stack.size() >= 64U)
          break;
        call_stack.push_back({next, graphics.offset_address});
        program_counter = relative_address(argument & 0x00fffffcU);
        if (implementation.verbose && submission == 0U)
          std::fprintf(
              stderr, "[psprism:ge] call from=%08x target=%08x return=%08x\n",
              next - 4U, program_counter, next);
        continue;
      } else if (command == 0x0bU) {
        if (call_stack.empty())
          break;
        const auto frame = call_stack.back();
        call_stack.pop_back();
        program_counter = frame.return_address;
        graphics.offset_address = frame.offset_address;
        if (implementation.verbose && submission == 0U)
          std::fprintf(stderr, "[psprism:ge] return target=%08x\n",
                       program_counter);
        continue;
      } else if (command == 0x0eU || command == 0x0fU) {
        const auto callback_id = static_cast<int>(state.gpr[6]);
        std::lock_guard callback_lock(implementation.objects_mutex);
        const auto callback = implementation.ge_callbacks.find(callback_id);
        if (callback != implementation.ge_callbacks.end()) {
          const auto signal = command == 0x0eU;
          const auto entry = signal ? callback->second.signal_entry
                                    : callback->second.finish_entry;
          const auto common_argument =
              signal ? callback->second.signal_argument
                     : callback->second.finish_argument;
          if (entry != 0U)
            pending_callbacks.push_back({entry, argument, common_argument});
        }
      } else if (command == 0x13U) {
        graphics.offset_address = argument << 8U;
      } else if (command == 0xeaU) {
        // A GE block transfer can modify a texture without changing any
        // texture-state command, so cached decodes are no longer valid.
        texture_cache.clear();
        const auto source_address =
            (graphics.commands[0xb2U] & 0x00fffff0U) |
            ((graphics.commands[0xb3U] & 0x00ff0000U) << 8U);
        const auto destination_address =
            (graphics.commands[0xb4U] & 0x00fffff0U) |
            ((graphics.commands[0xb5U] & 0x00ff0000U) << 8U);
        const auto source_stride = graphics.commands[0xb3U] & 0x7f8U;
        const auto destination_stride = graphics.commands[0xb5U] & 0x7f8U;
        const auto source_x = graphics.commands[0xebU] & 0x3ffU;
        const auto source_y = (graphics.commands[0xebU] >> 10U) & 0x3ffU;
        const auto destination_x = graphics.commands[0xecU] & 0x3ffU;
        const auto destination_y = (graphics.commands[0xecU] >> 10U) & 0x3ffU;
        const auto width = (graphics.commands[0xeeU] & 0x3ffU) + 1U;
        const auto height = ((graphics.commands[0xeeU] >> 10U) & 0x3ffU) + 1U;
        const auto bytes_per_pixel = (argument & 1U) != 0 ? 4U : 2U;
        for (std::uint32_t row = 0; row < height; ++row) {
          const auto source =
              source_address +
              ((source_y + row) * source_stride + source_x) * bytes_per_pixel;
          const auto destination =
              destination_address +
              ((destination_y + row) * destination_stride + destination_x) *
                  bytes_per_pixel;
          const auto row_size =
              static_cast<std::size_t>(width) * bytes_per_pixel;
          const auto* input =
              psprecomp::mapped_address(state, source, row_size);
          auto* output =
              psprecomp::mapped_address(state, destination, row_size);
          if (input == nullptr || output == nullptr)
            break;
          std::memmove(output, input, row_size);
        }
      }
      if (command == 0x0cU) {
        ++words;
        ended = true;
        break;
      }
      program_counter = next;
    }
    if (implementation.verbose && submission < 8U) {
      std::fprintf(stderr,
                   "[psprism:ge] list=%u address=%08x stall=%08x words=%u "
                   "prims=%u submitted=%u invalid=%u/%u/%u target=%08x "
                   "ended=%u commands=",
                   submission, state.gpr[4], state.gpr[5], words, primitives,
                   submitted_primitives, invalid_layouts, invalid_indices,
                   invalid_vertices, last_render_target, ended ? 1U : 0U);
      bool first = true;
      for (std::size_t command = 0; command < commands.size(); ++command) {
        if (commands[command] == 0)
          continue;
        std::fprintf(stderr, "%s%02zx:%u", first ? "" : ",", command,
                     commands[command]);
        first = false;
      }
      std::fputc('\n', stderr);
    }
    host::end_ge_frame();
    host::present_ge_frame();
  }
  for (const auto& callback : pending_callbacks) {
    dispatch_guest_callback(implementation, state, callback.entry,
                            callback.command_argument,
                            callback.common_argument);
  }
  state.gpr[2] = next_list++;
  return;
#else
  (void)implementation;
  state.gpr[2] = unimplemented;
#endif
}
