#include <psprism/psprism.hpp>

#include "host/host.hpp"
#include "utility_data.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace psprism {
namespace {

constexpr std::uint32_t unimplemented = 0x8002013aU;
constexpr std::uint32_t io_error = 0x80010005U;
constexpr std::uint32_t wait_timeout = 0x800201a8U;
constexpr std::uint32_t out_of_memory = 0x80020190U;
constexpr std::uint32_t utility_busy = 0x80110001U;
constexpr std::uint32_t utility_cancelled = 0x80110302U;
constexpr std::uint32_t return_address = 0xfffffff0U;

thread_local int current_thread_id = 1;

std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) {
  return (value + alignment - 1U) & ~(alignment - 1U);
}

template <typename T>
T* guest_pointer(psprecomp::State& state, std::uint32_t address) {
  return reinterpret_cast<T*>(
      psprecomp::mapped_address(state, address, sizeof(T)));
}

char* guest_string(psprecomp::State& state, std::uint32_t address) {
  address = psprecomp::canonical_address(address);
  if (address < state.memory_base) {
    return nullptr;
  }
  const auto offset = static_cast<std::size_t>(address - state.memory_base);
  if (offset >= state.memory_size ||
      std::memchr(state.memory + offset, 0, state.memory_size - offset) ==
          nullptr) {
    return nullptr;
  }
  return reinterpret_cast<char*>(state.memory + offset);
}

bool is_one_of(std::string_view value,
               std::initializer_list<std::string_view> choices) {
  for (const auto choice : choices) {
    if (value == choice) {
      return true;
    }
  }
  return false;
}

int host_open_flags(std::uint32_t flags) {
  int result = O_RDONLY;
  if ((flags & 3U) == 2U)
    result = O_WRONLY;
  if ((flags & 3U) == 3U)
    result = O_RDWR;
  if ((flags & 0x0100U) != 0)
    result |= O_APPEND;
  if ((flags & 0x0200U) != 0)
    result |= O_CREAT;
  if ((flags & 0x0400U) != 0)
    result |= O_TRUNC;
  return result;
}

struct VertexLayout {
  std::size_t stride{};
  std::size_t weight_offset{};
  std::size_t texture_offset{};
  std::size_t color_offset{};
  std::size_t normal_offset{};
  std::size_t position_offset{};
  std::uint32_t texture_type{};
  std::uint32_t color_type{};
  std::uint32_t normal_type{};
  std::uint32_t position_type{};
  std::uint32_t weight_type{};
  std::uint32_t weight_count{};
};

std::size_t component_size(std::uint32_t type) {
  return type == 1U ? 1U : type == 2U ? 2U : type == 3U ? 4U : 0U;
}

std::size_t align_offset(std::size_t value, std::size_t alignment) {
  return alignment == 0 ? value : (value + alignment - 1U) & ~(alignment - 1U);
}

VertexLayout vertex_layout(std::uint32_t type) {
  VertexLayout result;
  result.texture_type = type & 3U;
  result.color_type = (type >> 2U) & 7U;
  result.normal_type = (type >> 5U) & 3U;
  result.position_type = (type >> 7U) & 3U;
  result.weight_type = (type >> 9U) & 3U;
  result.weight_count = ((type >> 14U) & 7U) + 1U;
  const auto weight_size = component_size(result.weight_type);
  std::size_t offset{};
  std::size_t maximum_alignment{1U};
  if (weight_size != 0) {
    maximum_alignment = std::max(maximum_alignment, weight_size);
    offset = align_offset(offset, weight_size);
    result.weight_offset = offset;
    offset += weight_size * result.weight_count;
  }
  const auto texture_size = component_size(result.texture_type);
  if (texture_size != 0) {
    maximum_alignment = std::max(maximum_alignment, texture_size);
    offset = align_offset(offset, texture_size);
    result.texture_offset = offset;
    offset += texture_size * 2U;
  }
  const std::size_t color_size = result.color_type == 7U   ? 4U
                                 : result.color_type >= 4U ? 2U
                                                           : 0U;
  if (color_size != 0) {
    maximum_alignment = std::max(maximum_alignment, color_size);
    offset = align_offset(offset, color_size);
    result.color_offset = offset;
    offset += color_size;
  }
  const auto normal_size = component_size(result.normal_type);
  if (normal_size != 0) {
    maximum_alignment = std::max(maximum_alignment, normal_size);
    offset = align_offset(offset, normal_size);
    result.normal_offset = offset;
    offset += normal_size * 3U;
  }
  const auto position_size = component_size(result.position_type);
  maximum_alignment = std::max(maximum_alignment, position_size);
  offset = align_offset(offset, position_size);
  result.position_offset = offset;
  offset += position_size * 3U;
  result.stride = align_offset(offset, maximum_alignment);
  return result;
}

float float24(std::uint32_t value) {
  return std::bit_cast<float>((value & 0x00ffffffU) << 8U);
}

void transform43(const std::array<float, 12>& matrix, const float input[3],
                 float output[3]) {
  output[0] = matrix[0] * input[0] + matrix[3] * input[1] +
              matrix[6] * input[2] + matrix[9];
  output[1] = matrix[1] * input[0] + matrix[4] * input[1] +
              matrix[7] * input[2] + matrix[10];
  output[2] = matrix[2] * input[0] + matrix[5] * input[1] +
              matrix[8] * input[2] + matrix[11];
}

void transform_normal43(const std::array<float, 12>& matrix,
                        const float input[3], float output[3]) {
  output[0] = matrix[0] * input[0] + matrix[3] * input[1] +
              matrix[6] * input[2];
  output[1] = matrix[1] * input[0] + matrix[4] * input[1] +
              matrix[7] * input[2];
  output[2] = matrix[2] * input[0] + matrix[5] * input[1] +
              matrix[8] * input[2];
}

void normalize3(float value[3]) {
  const auto length = std::sqrt(value[0] * value[0] + value[1] * value[1] +
                                value[2] * value[2]);
  if (length > 0.0F) {
    value[0] /= length;
    value[1] /= length;
    value[2] /= length;
  }
}

void transform44(const std::array<float, 16>& matrix, const float input[3],
                 float output[4]) {
  output[0] = matrix[0] * input[0] + matrix[4] * input[1] +
              matrix[8] * input[2] + matrix[12];
  output[1] = matrix[1] * input[0] + matrix[5] * input[1] +
              matrix[9] * input[2] + matrix[13];
  output[2] = matrix[2] * input[0] + matrix[6] * input[1] +
              matrix[10] * input[2] + matrix[14];
  output[3] = matrix[3] * input[0] + matrix[7] * input[1] +
              matrix[11] * input[2] + matrix[15];
}

struct DecodedTexture {
  std::shared_ptr<std::vector<std::uint8_t>> pixels;
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t address{};
};

using TextureKey = std::array<std::uint32_t, 7>;

struct TextureKeyHash {
  std::size_t operator()(const TextureKey& key) const noexcept {
    std::size_t hash = 0xcbf29ce484222325ULL;
    for (const auto value : key) {
      hash ^= value;
      hash *= 0x100000001b3ULL;
    }
    return hash;
  }
};

std::uint32_t decode_16bit_color(std::uint16_t packed,
                                 std::uint32_t format) {
  const auto expand_4 = [](std::uint32_t value) { return value * 17U; };
  const auto expand_5 = [](std::uint32_t value) {
    return (value << 3U) | (value >> 2U);
  };
  const auto expand_6 = [](std::uint32_t value) {
    return (value << 2U) | (value >> 4U);
  };
  if (format == 0U) {
    return expand_5(packed & 31U) |
           (expand_6((packed >> 5U) & 63U) << 8U) |
           (expand_5((packed >> 11U) & 31U) << 16U) | 0xff000000U;
  }
  if (format == 1U) {
    return expand_5(packed & 31U) |
           (expand_5((packed >> 5U) & 31U) << 8U) |
           (expand_5((packed >> 10U) & 31U) << 16U) |
           ((packed & 0x8000U) != 0 ? 0xff000000U : 0U);
  }
  return expand_4(packed & 15U) |
         (expand_4((packed >> 4U) & 15U) << 8U) |
         (expand_4((packed >> 8U) & 15U) << 16U) |
         (expand_4((packed >> 12U) & 15U) << 24U);
}

DecodedTexture decode_texture(
    psprecomp::State& state,
    const std::array<std::uint32_t, 256>& commands,
    const std::array<std::uint8_t, 1024>& clut) {
  DecodedTexture result;
  const auto format = commands[0xc3U] & 0xfU;
  result.address = (commands[0xa0U] & 0x00fffff0U) |
                   ((commands[0xa8U] << 8U) & 0x0f000000U);
  if ((commands[0x1eU] & 1U) == 0 || format > 5U)
    return result;
  result.width = 1U << (commands[0xb8U] & 0xfU);
  result.height = 1U << ((commands[0xb8U] >> 8U) & 0xfU);
  const auto buffer_width = commands[0xa8U] & 0x3ffU;
  if (result.width == 0 || result.height == 0 || buffer_width == 0 ||
      result.width > 1024U || result.height > 1024U)
    return {};
  const auto row_bytes =
      format == 4U ? (buffer_width + 1U) / 2U
                   : buffer_width * (format == 3U ? 4U : format < 4U ? 2U : 1U);
  const auto blocks_per_row = (row_bytes + 15U) / 16U;
  const auto block_rows = (result.height + 7U) / 8U;
  const auto swizzled = (commands[0xc2U] & 1U) != 0;
  const auto source_size = swizzled
                               ? static_cast<std::size_t>(blocks_per_row) *
                                     block_rows * 128U
                               : static_cast<std::size_t>(row_bytes) *
                                     result.height;
  const auto* source =
      psprecomp::mapped_address(state, result.address, source_size);
  if (source == nullptr)
    return {};
  const std::uint8_t* palette{};
  auto palette_format = commands[0xc5U] & 3U;
  if (format >= 4U) {
    palette = clut.data();
  }
  const auto shift = (commands[0xc5U] >> 2U) & 0x1fU;
  const auto mask = (commands[0xc5U] >> 8U) & 0xffU;
  const auto start = ((commands[0xc5U] >> 16U) & 0x1fU) << 4U;
  result.pixels = std::make_shared<std::vector<std::uint8_t>>(
      static_cast<std::size_t>(result.width) * result.height * 4U);
  for (std::uint32_t y = 0; y < result.height; ++y) {
    for (std::uint32_t x = 0; x < result.width; ++x) {
      std::uint32_t color{};
      const auto byte_x = format == 4U ? x / 2U
                          : format == 3U ? x * 4U
                          : format < 4U  ? x * 2U
                                       : x;
      const auto source_offset =
          swizzled ? ((y / 8U) * blocks_per_row + byte_x / 16U) * 128U +
                         (y & 7U) * 16U + (byte_x & 15U)
                   : static_cast<std::size_t>(y) * row_bytes + byte_x;
      if (format == 3U) {
        std::memcpy(&color, source + source_offset, sizeof(color));
      } else if (format < 3U) {
        std::uint16_t packed{};
        std::memcpy(&packed, source + source_offset, sizeof(packed));
        color = decode_16bit_color(packed, format);
      } else {
        auto palette_source = source[source_offset];
        if (format == 4U)
          palette_source =
              x % 2U == 0 ? palette_source & 15U : palette_source >> 4U;
        const auto palette_index =
            ((palette_source >> shift) & mask) | (start & 0xffU);
        if (palette_format == 3U) {
          std::memcpy(&color, palette + palette_index * 4U, sizeof(color));
        } else {
          std::uint16_t packed{};
          std::memcpy(&packed, palette + palette_index * 2U, sizeof(packed));
          color = decode_16bit_color(packed, palette_format);
        }
      }
      const auto output = (static_cast<std::size_t>(y) * result.width + x) * 4U;
      (*result.pixels)[output] = static_cast<std::uint8_t>(color);
      (*result.pixels)[output + 1U] = static_cast<std::uint8_t>(color >> 8U);
      (*result.pixels)[output + 2U] = static_cast<std::uint8_t>(color >> 16U);
      (*result.pixels)[output + 3U] =
          static_cast<std::uint8_t>(color >> 24U);
    }
  }
  return result;
}

struct UtilityText {
  const char* load;
  const char* save;
  const char* remove;
  const char* message;
  const char* keyboard;
  const char* accept;
  const char* back;
  const char* yes;
  const char* no;
  const char* empty;
};

UtilityText utility_text(std::uint32_t language) {
  static constexpr UtilityText translations[] = {
      {"セーブデータをロード", "セーブデータを保存", "セーブデータを削除",
       "メッセージ", "文字入力", "決定", "戻る", "はい", "いいえ", "空きスロット"},
      {"Load saved data", "Save data", "Delete saved data", "Message",
       "Text input", "OK", "Back", "Yes", "No", "Empty slot"},
      {"Charger les données", "Sauvegarder", "Supprimer la sauvegarde",
       "Message", "Saisie de texte", "OK", "Retour", "Oui", "Non", "Emplacement vide"},
      {"Cargar datos", "Guardar datos", "Eliminar datos", "Mensaje",
       "Entrada de texto", "Aceptar", "Atrás", "Sí", "No", "Ranura vacía"},
      {"Spielstand laden", "Spielstand speichern", "Spielstand löschen",
       "Meldung", "Texteingabe", "OK", "Zurück", "Ja", "Nein", "Leerer Speicherplatz"},
      {"Carica dati", "Salva dati", "Elimina dati", "Messaggio",
       "Inserimento testo", "OK", "Indietro", "Sì", "No", "Slot vuoto"},
      {"Gegevens laden", "Gegevens opslaan", "Gegevens verwijderen",
       "Bericht", "Tekstinvoer", "OK", "Terug", "Ja", "Nee", "Leeg slot"},
      {"Carregar dados", "Guardar dados", "Eliminar dados", "Mensagem",
       "Introdução de texto", "OK", "Voltar", "Sim", "Não", "Espaço vazio"},
      {"Загрузить данные", "Сохранить данные", "Удалить данные", "Сообщение",
       "Ввод текста", "ОК", "Назад", "Да", "Нет", "Пустая ячейка"},
      {"저장 데이터 불러오기", "데이터 저장", "저장 데이터 삭제", "메시지",
       "문자 입력", "확인", "뒤로", "예", "아니요", "빈 슬롯"},
      {"載入保存資料", "保存資料", "刪除保存資料", "訊息", "文字輸入",
       "確定", "返回", "是", "否", "空白欄位"},
      {"载入保存数据", "保存数据", "删除保存数据", "消息", "文字输入",
       "确定", "返回", "是", "否", "空白栏位"},
  };
  return translations[language < std::size(translations) ? language : 1U];
}

std::string fixed_string(const std::uint8_t* value, std::size_t size) {
  const auto* end = static_cast<const std::uint8_t*>(
      std::memchr(value, 0, size));
  return std::string(reinterpret_cast<const char*>(value),
                     end == nullptr ? size
                                    : static_cast<std::size_t>(end - value));
}

std::u16string guest_utf16(psprecomp::State& state, std::uint32_t address,
                           std::size_t maximum = 4096U) {
  std::u16string result;
  if (address == 0) return result;
  for (std::size_t index = 0; index < maximum; ++index) {
    const auto* value = psprecomp::mapped_address(
        state, address + static_cast<std::uint32_t>(index * 2U), 2U);
    if (value == nullptr) return {};
    std::uint16_t character{};
    std::memcpy(&character, value, sizeof(character));
    if (character == 0) break;
    result.push_back(static_cast<char16_t>(character));
  }
  return result;
}

std::string utf16_to_utf8(std::u16string_view value) {
  std::string result;
  for (std::size_t index = 0; index < value.size(); ++index) {
    std::uint32_t character = value[index];
    if (character >= 0xd800U && character <= 0xdbffU &&
        index + 1U < value.size()) {
      const auto low = static_cast<std::uint32_t>(value[index + 1U]);
      if (low >= 0xdc00U && low <= 0xdfffU) {
        character = 0x10000U + ((character - 0xd800U) << 10U) +
                    (low - 0xdc00U);
        ++index;
      }
    }
    if (character <= 0x7fU) {
      result.push_back(static_cast<char>(character));
    } else if (character <= 0x7ffU) {
      result.push_back(static_cast<char>(0xc0U | (character >> 6U)));
      result.push_back(static_cast<char>(0x80U | (character & 0x3fU)));
    } else if (character <= 0xffffU) {
      result.push_back(static_cast<char>(0xe0U | (character >> 12U)));
      result.push_back(static_cast<char>(0x80U | ((character >> 6U) & 0x3fU)));
      result.push_back(static_cast<char>(0x80U | (character & 0x3fU)));
    } else {
      result.push_back(static_cast<char>(0xf0U | (character >> 18U)));
      result.push_back(static_cast<char>(0x80U | ((character >> 12U) & 0x3fU)));
      result.push_back(static_cast<char>(0x80U | ((character >> 6U) & 0x3fU)));
      result.push_back(static_cast<char>(0x80U | (character & 0x3fU)));
    }
  }
  return result;
}

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path,
                                           std::size_t maximum = 16U << 20U) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > maximum) return {};
  std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
  std::ifstream input(path, std::ios::binary);
  if (!result.empty())
    input.read(reinterpret_cast<char*>(result.data()), result.size());
  return input ? result : std::vector<std::uint8_t>{};
}

std::string file_timestamp(const std::filesystem::path& path) {
  std::error_code error;
  const auto file_time = std::filesystem::last_write_time(path, error);
  if (error) return {};
  const auto system_time = std::chrono::time_point_cast<
      std::chrono::system_clock::duration>(
      file_time - decltype(file_time)::clock::now() +
      std::chrono::system_clock::now());
  const auto time = std::chrono::system_clock::to_time_t(system_time);
  std::tm local{};
  if (localtime_r(&time, &local) == nullptr) return {};
  char formatted[32]{};
  return std::strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M", &local)
             ? formatted
             : std::string{};
}

std::string directory_size_label(const std::filesystem::path& path) {
  std::error_code error;
  std::uintmax_t bytes{};
  for (std::filesystem::recursive_directory_iterator iterator(path, error), end;
       iterator != end && !error; iterator.increment(error)) {
    if (iterator->is_regular_file(error)) bytes += iterator->file_size(error);
  }
  if (error) return {};
  const auto kibibytes = std::max<std::uintmax_t>(1U, (bytes + 1023U) / 1024U);
  return std::to_string(kibibytes) + " KB";
}

} // namespace

struct Runtime::Implementation {
  struct GuestThread {
    int uid{};
    std::string name;
    std::uint32_t entry{};
    std::uint32_t priority{};
    std::uint32_t stack_address{};
    std::uint32_t stack_size{};
    std::uint32_t tls_address{};
    std::shared_ptr<psprecomp::State> state;
    std::thread host_thread;
    std::atomic<bool> finished{};
    std::int32_t result{};
  };

  struct Semaphore {
    std::mutex mutex;
    std::condition_variable changed;
    int count{};
    int maximum{};
  };

  struct EventFlag {
    std::mutex mutex;
    std::condition_variable changed;
    std::uint32_t bits{};
  };

  struct MemoryBlock {
    std::uint32_t address{};
    std::uint32_t size{};
  };

  struct FixedPool {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::uint32_t> available;
  };

  struct VariablePool {
    std::mutex mutex;
    std::condition_variable changed;
    MemoryBlock backing;
    std::vector<MemoryBlock> available;
    std::unordered_map<std::uint32_t, std::uint32_t> allocated;
  };

  struct Directory {
    std::vector<std::filesystem::directory_entry> entries;
    std::size_t next{};
  };

  struct Callback {
    std::uint32_t entry{};
    std::uint32_t common_argument{};
  };

  struct GraphicsState {
    std::mutex mutex;
    std::array<std::uint32_t, 256> commands{};
    std::uint32_t vertex_address{};
    std::uint32_t index_address{};
    std::uint32_t offset_address{};
    std::array<float, 12> world_matrix{};
    std::array<float, 12> view_matrix{};
    std::array<float, 16> projection_matrix{};
    std::array<float, 96> bone_matrices{};
    std::array<std::uint8_t, 1024> clut{};
    std::uint32_t world_matrix_index{};
    std::uint32_t view_matrix_index{};
    std::uint32_t projection_matrix_index{};
    std::uint32_t bone_matrix_index{};
  };

  std::uint8_t* memory{};
  std::size_t memory_size{};
  std::uint32_t memory_base{};
  std::vector<std::uint8_t> scratchpad;
  std::vector<std::uint8_t> video_memory;
  Configuration configuration;
  std::unordered_set<std::string> warned;
  std::unordered_map<int, int> files;
  std::unordered_map<int, std::uint64_t> file_bases;
  std::unordered_map<int, std::int64_t> async_results;
  std::unordered_map<int, Directory> directories;
  std::filesystem::path current_directory;
  std::filesystem::path disc_image;
  std::unordered_map<std::string, std::uint32_t> disc_sectors;
  std::mutex objects_mutex;
  std::unordered_map<int, std::shared_ptr<GuestThread>> threads;
  std::unordered_map<int, std::shared_ptr<Semaphore>> semaphores;
  std::unordered_map<int, std::shared_ptr<EventFlag>> event_flags;
  std::unordered_map<int, MemoryBlock> memory_blocks;
  std::unordered_map<int, std::shared_ptr<FixedPool>> fixed_pools;
  std::unordered_map<int, std::shared_ptr<VariablePool>> variable_pools;
  std::unordered_map<int, Callback> callbacks;
  int next_file{3};
  int next_uid{0x100};
  std::uint32_t heap_cursor{};
  std::vector<MemoryBlock> free_heap_blocks;
  std::uint32_t stack_cursor{};
  std::atomic<bool> exit_requested{};
  bool verbose{};
  std::atomic<std::uint32_t> displayed_frames{};
  std::atomic<std::uint32_t> submitted_ge_lists{};
  std::uint64_t start_monotonic_microseconds{};
  std::uint64_t start_unix_seconds{};
  GraphicsState graphics;
  std::atomic<std::uint32_t> savedata_status{};
  std::uint32_t savedata_parameters{};
  bool savedata_operation_complete{};
  std::uint64_t savedata_dialog_id{};
  bool savedata_dialog_presented{};
  std::vector<std::string> savedata_names;
  std::atomic<std::uint32_t> message_dialog_status{};
  std::uint32_t message_dialog_parameters{};
  std::uint64_t message_dialog_id{};
  std::atomic<std::uint32_t> osk_status{};
  std::uint32_t osk_parameters{};
  std::uint64_t osk_dialog_id{};
  std::atomic<std::uint32_t> active_utility{};
  std::uint64_t next_dialog_id{1U};

  int allocate_uid() { return next_uid++; }

  std::uint64_t elapsed_microseconds() const {
    return host::monotonic_microseconds() - start_monotonic_microseconds;
  }

  std::uint32_t allocate_heap(std::uint32_t size,
                              std::uint32_t alignment = 64) {
    for (std::size_t index = 0; index < free_heap_blocks.size(); ++index) {
      const auto block = free_heap_blocks[index];
      const auto address = align_up(block.address, alignment);
      if (address < block.address || address - block.address > block.size ||
          size > block.size - (address - block.address))
        continue;
      free_heap_blocks.erase(free_heap_blocks.begin() + index);
      if (address > block.address)
        free_heap_blocks.push_back({block.address, address - block.address});
      const auto end = address + size;
      const auto block_end = block.address + block.size;
      if (end < block_end)
        free_heap_blocks.push_back({end, block_end - end});
      return address;
    }
    const auto address = align_up(heap_cursor, alignment);
    if (size > stack_cursor || address > stack_cursor - size)
      return 0;
    heap_cursor = address + size;
    return memory_base + address;
  }

  void free_heap(std::uint32_t address, std::uint32_t size) {
    if (address == 0 || size == 0)
      return;
    free_heap_blocks.push_back({address, size});
    std::sort(free_heap_blocks.begin(), free_heap_blocks.end(),
              [](const MemoryBlock& left, const MemoryBlock& right) {
                return left.address < right.address;
              });
    for (std::size_t index = 1; index < free_heap_blocks.size();) {
      auto& previous = free_heap_blocks[index - 1U];
      const auto& current = free_heap_blocks[index];
      if (previous.address + previous.size == current.address) {
        previous.size += current.size;
        free_heap_blocks.erase(free_heap_blocks.begin() + index);
      } else {
        ++index;
      }
    }
  }

  std::uint32_t allocate_stack(std::uint32_t size) {
    size = align_up(size, 64);
    if (size > stack_cursor || stack_cursor - size < heap_cursor)
      return 0;
    stack_cursor -= size;
    return memory_base + stack_cursor;
  }

  std::filesystem::path resolve_path(std::string_view psp_path) const {
    std::string normalized(psp_path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    psp_path = normalized;
    const auto suffix = [](std::string_view value, std::string_view prefix) {
      value.remove_prefix(prefix.size());
      while (!value.empty() && value.front() == '/')
        value.remove_prefix(1);
      return value;
    };
    if (psp_path.starts_with("disc0:") || psp_path.starts_with("umd0:")) {
      const auto prefix = psp_path.starts_with("disc0:") ? "disc0:" : "umd0:";
      return configuration.disc_root / suffix(psp_path, prefix);
    }
    if (psp_path.starts_with("ms0:")) {
      return configuration.writable_root / suffix(psp_path, "ms0:");
    }
    if (!psp_path.empty() && psp_path.front() == '/')
      return configuration.disc_root / suffix(psp_path, "");
    return (current_directory.empty() ? configuration.disc_root
                                      : current_directory) /
           suffix(psp_path, "");
  }

  int descriptor(int psp_descriptor) const {
    if (psp_descriptor >= 0 && psp_descriptor <= 2)
      return psp_descriptor;
    const auto found = files.find(psp_descriptor);
    return found == files.end() ? -1 : found->second;
  }
};

Runtime& Runtime::instance() {
  static Runtime runtime;
  return runtime;
}

Runtime::Runtime() : implementation_(new Implementation) {}

Runtime::~Runtime() {
  wait_for_guest_threads();
  for (const auto& [psp_descriptor, host_descriptor] : implementation_->files) {
    static_cast<void>(psp_descriptor);
    ::close(host_descriptor);
  }
  delete implementation_;
}

void Runtime::set_verbose(bool enabled) {
  implementation_->verbose = enabled;
  host::set_verbose_logging(enabled);
}

void Runtime::configure(std::uint8_t* memory, std::size_t size,
                        std::uint32_t base, Configuration configuration) {
  implementation_->memory = memory;
  implementation_->memory_size = size;
  implementation_->memory_base = base;
  implementation_->scratchpad.assign(16U * 1024U, 0);
  implementation_->video_memory.assign(2U * 1024U * 1024U, 0);
  implementation_->start_monotonic_microseconds =
      host::monotonic_microseconds();
  implementation_->start_unix_seconds = host::unix_seconds();
  implementation_->heap_cursor =
      align_up(static_cast<std::uint32_t>(configuration.image_size), 64U);
  implementation_->stack_cursor = static_cast<std::uint32_t>(size);
  constexpr std::uint32_t user_memory_start = 0x08800000U;
  const auto image_start = base + configuration.image_start;
  if (base <= user_memory_start && image_start > user_memory_start &&
      image_start <= base + size) {
    implementation_->free_heap_blocks.push_back(
        {user_memory_start, image_start - user_memory_start});
  }
  if (configuration.disc_root.empty()) {
    if (const auto* value = std::getenv("PSPRISM_DISC_ROOT")) {
      configuration.disc_root = value;
    } else {
      configuration.disc_root = std::filesystem::current_path() / "disc";
    }
  }
  if (configuration.writable_root.empty()) {
    if (const auto* value = std::getenv("PSPRISM_WRITABLE_ROOT")) {
      configuration.writable_root = value;
    } else {
      configuration.writable_root =
          std::filesystem::current_path() / ".psprism" / "ms0";
    }
  }
  std::filesystem::create_directories(configuration.writable_root);
  implementation_->configuration = std::move(configuration);
  implementation_->current_directory = implementation_->configuration.disc_root;
  if (const auto* value = std::getenv("PSPRISM_DISC_IMAGE")) {
    implementation_->disc_image = value;
  } else {
    implementation_->disc_image =
        implementation_->configuration.disc_root.parent_path() / "original" /
        "disc.iso";
  }
  {
    std::ifstream sectors(implementation_->disc_image.parent_path() /
                          "disc-sectors.tsv");
    std::string line;
    while (std::getline(sectors, line)) {
      const auto separator = line.find('\t');
      if (separator == std::string::npos)
        continue;
      try {
        implementation_->disc_sectors.emplace(
            line.substr(separator + 1U),
            static_cast<std::uint32_t>(std::stoul(line.substr(0, separator))));
      } catch (const std::exception&) {
      }
    }
  }
  host::initialize_frontend();
  if (implementation_->verbose) {
    std::fprintf(stderr, "[psprism:macos] disc=%s writable=%s\n",
                 implementation_->configuration.disc_root.c_str(),
                 implementation_->configuration.writable_root.c_str());
  }
}

void Runtime::log(const char* format, std::uint32_t first,
                  std::uint32_t second) {
  if (implementation_->verbose)
    std::fprintf(stderr, format, first, second);
}

void Runtime::prepare_state(psprecomp::State& state) {
  state.scratchpad = implementation_->scratchpad.data();
  state.scratchpad_size = implementation_->scratchpad.size();
  state.video_memory = implementation_->video_memory.data();
  state.video_memory_size = implementation_->video_memory.size();
}

void Runtime::run_host_loop() {
  host::run_event_loop();
  implementation_->exit_requested = true;
  wait_for_guest_threads();
}

void Runtime::wait_for_guest_threads() {
  for (;;) {
    std::vector<std::shared_ptr<Implementation::GuestThread>> pending;
    {
      std::lock_guard lock(implementation_->objects_mutex);
      for (const auto& [uid, thread] : implementation_->threads) {
        static_cast<void>(uid);
        if (thread->host_thread.joinable())
          pending.push_back(thread);
      }
    }
    if (pending.empty())
      return;
    for (const auto& thread : pending) {
      if (thread->host_thread.joinable())
        thread->host_thread.join();
    }
  }
}

void Runtime::dispatch(psprecomp::State& state, std::string_view name) {
  state.gpr[2] = unimplemented;

  const auto notify_callback = [&](int uid, std::uint32_t argument) {
    Implementation::Callback callback;
    std::uint32_t stack = 0;
    {
      std::lock_guard lock(implementation_->objects_mutex);
      const auto found = implementation_->callbacks.find(uid);
      if (found == implementation_->callbacks.end() ||
          !implementation_->configuration.guest_executor)
        return false;
      callback = found->second;
      stack = implementation_->allocate_stack(0x4000U);
    }
    if (stack == 0)
      return false;
    auto callback_state = state;
    callback_state.pc = callback.entry;
    std::fill_n(callback_state.gpr, 32U, 0U);
    callback_state.gpr[4] = static_cast<std::uint32_t>(uid);
    callback_state.gpr[5] = argument;
    callback_state.gpr[6] = callback.common_argument;
    callback_state.gpr[29] = stack + 0x4000U;
    callback_state.gpr[31] = return_address;
    callback_state.stop_reason = psprecomp::StopReason::running;
    implementation_->configuration.guest_executor(callback_state);
    return true;
  };

  if (name == "sceKernelCreateThread") {
    const auto* thread_name = guest_string(state, state.gpr[4]);
    const auto requested_stack = std::max<std::uint32_t>(state.gpr[7], 0x4000U);
    std::lock_guard lock(implementation_->objects_mutex);
    const auto stack = implementation_->allocate_stack(requested_stack);
    const auto tls = implementation_->allocate_heap(0x100U, 64U);
    if (stack == 0 || tls == 0 ||
        !implementation_->configuration.guest_executor) {
      state.gpr[2] = out_of_memory;
      return;
    }
    auto thread = std::make_shared<Implementation::GuestThread>();
    thread->uid = implementation_->allocate_uid();
    thread->name = thread_name != nullptr ? thread_name : "guest-thread";
    thread->entry = state.gpr[5];
    thread->priority = state.gpr[6];
    thread->stack_address = stack;
    thread->stack_size = requested_stack;
    thread->tls_address = tls;
    implementation_->threads.emplace(thread->uid, thread);
    state.gpr[2] = static_cast<std::uint32_t>(thread->uid);
    if (implementation_->verbose)
      std::fprintf(stderr,
                   "[psprism:thread] create uid=%d name=%s entry=%08x\n",
                   thread->uid, thread->name.c_str(), thread->entry);
    return;
  }
  if (name == "sceKernelStartThread") {
    std::shared_ptr<Implementation::GuestThread> thread;
    {
      std::lock_guard lock(implementation_->objects_mutex);
      const auto found =
          implementation_->threads.find(static_cast<int>(state.gpr[4]));
      if (found == implementation_->threads.end() ||
          found->second->host_thread.joinable())
        return;
      thread = found->second;
    }
    const auto argument_size = state.gpr[5];
    const auto argument_pointer = state.gpr[6];
    if (implementation_->verbose)
      std::fprintf(stderr,
                   "[psprism:thread] launch uid=%d args=%u argp=%08x\n",
                   thread->uid, argument_size, argument_pointer);
    thread->state = std::make_shared<psprecomp::State>();
    thread->state->memory = implementation_->memory;
    thread->state->memory_size = implementation_->memory_size;
    thread->state->memory_base = implementation_->memory_base;
    thread->state->direct_memory_access = false;
    prepare_state(*thread->state);
    thread->state->pc = thread->entry;
    thread->state->gpr[4] = argument_size;
    thread->state->gpr[5] = argument_pointer;
    thread->state->gpr[26] = thread->tls_address;
    thread->state->gpr[27] = thread->tls_address + 0x80U;
    thread->state->gpr[29] = thread->stack_address + thread->stack_size - 64U;
    thread->state->gpr[31] = return_address;
    const auto executor = implementation_->configuration.guest_executor;
    const auto verbose = implementation_->verbose;
    thread->host_thread = std::thread([thread, executor, verbose] {
      current_thread_id = thread->uid;
      if (verbose)
        std::fprintf(stderr, "[psprism:thread] start uid=%d name=%s\n",
                     thread->uid, thread->name.c_str());
      executor(*thread->state);
      thread->result = static_cast<std::int32_t>(thread->state->gpr[2]);
      thread->finished = true;
      if (verbose)
        std::fprintf(
            stderr,
            "[psprism:thread] stop uid=%d reason=%u pc=%08x result=%d "
            "fault=%08x fault_pc=%08x insn=%08x sp=%08x ra=%08x "
            "a0=%08x a1=%08x a2=%08x a3=%08x\n",
            thread->uid, static_cast<unsigned>(thread->state->stop_reason),
            thread->state->pc, thread->result, thread->state->fault_address,
            thread->state->fault_pc, thread->state->fault_instruction,
            thread->state->gpr[29], thread->state->gpr[31],
            thread->state->gpr[4], thread->state->gpr[5],
            thread->state->gpr[6], thread->state->gpr[7]);
    });
    state.gpr[2] = 0;
    return;
  }
  if (name == "sceKernelGetThreadId") {
    state.gpr[2] = static_cast<std::uint32_t>(current_thread_id);
    return;
  }
  if (name == "sceKernelWaitThreadEnd") {
    std::shared_ptr<Implementation::GuestThread> thread;
    {
      std::lock_guard lock(implementation_->objects_mutex);
      const auto found =
          implementation_->threads.find(static_cast<int>(state.gpr[4]));
      if (found == implementation_->threads.end())
        return;
      thread = found->second;
    }
    if (thread->host_thread.joinable() && thread->uid != current_thread_id)
      thread->host_thread.join();
    state.gpr[2] = 0;
    return;
  }
  if (is_one_of(name, {"sceKernelTerminateThread",
                       "sceKernelTerminateDeleteThread"})) {
    std::lock_guard lock(implementation_->objects_mutex);
    const auto found =
        implementation_->threads.find(static_cast<int>(state.gpr[4]));
    if (found == implementation_->threads.end())
      return;
    if (found->second->state)
      found->second->state->stop_reason = psprecomp::StopReason::returned;
    state.gpr[2] = 0;
    return;
  }
  if (name == "sceKernelDeleteThread") {
    std::lock_guard lock(implementation_->objects_mutex);
    const auto found =
        implementation_->threads.find(static_cast<int>(state.gpr[4]));
    if (found == implementation_->threads.end() ||
        found->second->host_thread.joinable())
      return;
    implementation_->threads.erase(found);
    state.gpr[2] = 0;
    return;
  }

  if (name == "sceKernelCreateSema") {
    auto semaphore = std::make_shared<Implementation::Semaphore>();
    semaphore->count = static_cast<int>(state.gpr[6]);
    semaphore->maximum = static_cast<int>(state.gpr[7]);
    std::lock_guard lock(implementation_->objects_mutex);
    const auto uid = implementation_->allocate_uid();
    implementation_->semaphores.emplace(uid, semaphore);
    state.gpr[2] = static_cast<std::uint32_t>(uid);
    return;
  }
  if (is_one_of(name, {"sceKernelWaitSema", "sceKernelWaitSemaCB",
                       "sceKernelPollSema"})) {
    std::shared_ptr<Implementation::Semaphore> semaphore;
    {
      std::lock_guard lock(implementation_->objects_mutex);
      const auto found =
          implementation_->semaphores.find(static_cast<int>(state.gpr[4]));
      if (found == implementation_->semaphores.end())
        return;
      semaphore = found->second;
    }
    const auto amount = static_cast<int>(state.gpr[5]);
    std::unique_lock lock(semaphore->mutex);
    if (name == "sceKernelPollSema" && semaphore->count < amount) {
      state.gpr[2] = wait_timeout;
      return;
    }
    semaphore->changed.wait(lock, [&] {
      return semaphore->count >= amount || implementation_->exit_requested;
    });
    if (!implementation_->exit_requested)
      semaphore->count -= amount;
    state.gpr[2] = 0;
    return;
  }
  if (name == "sceKernelSignalSema") {
    std::shared_ptr<Implementation::Semaphore> semaphore;
    {
      std::lock_guard lock(implementation_->objects_mutex);
      const auto found =
          implementation_->semaphores.find(static_cast<int>(state.gpr[4]));
      if (found == implementation_->semaphores.end())
        return;
      semaphore = found->second;
    }
    {
      std::lock_guard lock(semaphore->mutex);
      semaphore->count =
          std::min(semaphore->maximum,
                   semaphore->count + static_cast<int>(state.gpr[5]));
    }
    semaphore->changed.notify_all();
    state.gpr[2] = 0;
    return;
  }
  if (name == "sceKernelDeleteSema") {
    std::lock_guard lock(implementation_->objects_mutex);
    state.gpr[2] =
        implementation_->semaphores.erase(static_cast<int>(state.gpr[4]))
            ? 0U
            : unimplemented;
    return;
  }

  if (name == "sceKernelCreateEventFlag") {
    auto event = std::make_shared<Implementation::EventFlag>();
    event->bits = state.gpr[6];
    std::lock_guard lock(implementation_->objects_mutex);
    const auto uid = implementation_->allocate_uid();
    implementation_->event_flags.emplace(uid, event);
    state.gpr[2] = static_cast<std::uint32_t>(uid);
    return;
  }
  if (is_one_of(name, {"sceKernelSetEventFlag", "sceKernelClearEventFlag"})) {
    std::shared_ptr<Implementation::EventFlag> event;
    {
      std::lock_guard lock(implementation_->objects_mutex);
      const auto found =
          implementation_->event_flags.find(static_cast<int>(state.gpr[4]));
      if (found == implementation_->event_flags.end())
        return;
      event = found->second;
    }
    {
      std::lock_guard lock(event->mutex);
      if (name == "sceKernelSetEventFlag")
        event->bits |= state.gpr[5];
      else
        event->bits &= state.gpr[5];
    }
    event->changed.notify_all();
    state.gpr[2] = 0;
    return;
  }
  if (name == "sceKernelWaitEventFlag") {
    std::shared_ptr<Implementation::EventFlag> event;
    {
      std::lock_guard lock(implementation_->objects_mutex);
      const auto found =
          implementation_->event_flags.find(static_cast<int>(state.gpr[4]));
      if (found == implementation_->event_flags.end())
        return;
      event = found->second;
    }
    const auto pattern = state.gpr[5];
    const auto mode = state.gpr[6];
    const auto matched = [&] {
      return (mode & 1U) != 0 ? (event->bits & pattern) != 0
                              : (event->bits & pattern) == pattern;
    };
    std::unique_lock lock(event->mutex);
    event->changed.wait(
        lock, [&] { return matched() || implementation_->exit_requested; });
    const auto observed = event->bits;
    if (auto* output = guest_pointer<std::uint32_t>(state, state.gpr[7]))
      *output = observed;
    if ((mode & 0x20U) != 0)
      event->bits = 0;
    else if ((mode & 0x10U) != 0)
      event->bits &= ~pattern;
    state.gpr[2] = 0;
    return;
  }
  if (name == "sceKernelDeleteEventFlag") {
    std::lock_guard lock(implementation_->objects_mutex);
    state.gpr[2] =
        implementation_->event_flags.erase(static_cast<int>(state.gpr[4]))
            ? 0U
            : unimplemented;
    return;
  }

  if (name == "sceKernelAllocPartitionMemory") {
    std::lock_guard lock(implementation_->objects_mutex);
    const auto size = state.gpr[7];
    const auto address = implementation_->allocate_heap(size);
    if (address == 0) {
      state.gpr[2] = out_of_memory;
      return;
    }
    const auto uid = implementation_->allocate_uid();
    implementation_->memory_blocks.emplace(
        uid, Implementation::MemoryBlock{address, size});
    state.gpr[2] = static_cast<std::uint32_t>(uid);
    if (implementation_->verbose)
      std::fprintf(stderr,
                   "[psprism:memory] partition uid=%d size=%u address=%08x\n",
                   uid, size, address);
    return;
  }
  if (name == "sceKernelGetBlockHeadAddr") {
    std::lock_guard lock(implementation_->objects_mutex);
    const auto found =
        implementation_->memory_blocks.find(static_cast<int>(state.gpr[4]));
    if (found != implementation_->memory_blocks.end())
      state.gpr[2] = found->second.address;
    return;
  }
  if (name == "sceKernelFreePartitionMemory") {
    std::lock_guard lock(implementation_->objects_mutex);
    state.gpr[2] =
        implementation_->memory_blocks.erase(static_cast<int>(state.gpr[4]))
            ? 0U
            : unimplemented;
    return;
  }
  if (name == "sceKernelMaxFreeMemSize") {
    std::lock_guard lock(implementation_->objects_mutex);
    auto largest =
        implementation_->stack_cursor > implementation_->heap_cursor
            ? implementation_->stack_cursor - implementation_->heap_cursor
            : 0U;
    for (const auto& block : implementation_->free_heap_blocks)
      largest = std::max(largest, block.size);
    state.gpr[2] = largest;
    return;
  }
  if (name == "sceKernelCreateFpl") {
    const auto block_size = state.gpr[7];
    const auto block_count = state.gpr[8];
    if (implementation_->verbose)
      std::fprintf(stderr,
                   "[psprism:memory] create-fpl block_size=%u blocks=%u "
                   "sp=%08x\n",
                   block_size, block_count, state.gpr[29]);
    if (block_size == 0 || block_count == 0) {
      state.gpr[2] = out_of_memory;
      return;
    }
    auto pool = std::make_shared<Implementation::FixedPool>();
    std::lock_guard lock(implementation_->objects_mutex);
    for (std::uint32_t index = 0; index < block_count; ++index) {
      const auto address = implementation_->allocate_heap(block_size);
      if (address == 0) {
        state.gpr[2] = out_of_memory;
        return;
      }
      pool->available.push_back(address);
    }
    const auto uid = implementation_->allocate_uid();
    implementation_->fixed_pools.emplace(uid, pool);
    state.gpr[2] = static_cast<std::uint32_t>(uid);
    if (implementation_->verbose)
      std::fprintf(stderr, "[psprism:memory] fpl uid=%d\n", uid);
    return;
  }
  if (name == "sceKernelAllocateFpl") {
    std::shared_ptr<Implementation::FixedPool> pool;
    {
      std::lock_guard lock(implementation_->objects_mutex);
      const auto found =
          implementation_->fixed_pools.find(static_cast<int>(state.gpr[4]));
      if (found == implementation_->fixed_pools.end())
        return;
      pool = found->second;
    }
    std::unique_lock lock(pool->mutex);
    pool->changed.wait(lock, [&] {
      return !pool->available.empty() || implementation_->exit_requested;
    });
    if (pool->available.empty())
      return;
    const auto address = pool->available.back();
    pool->available.pop_back();
    if (auto* output = guest_pointer<std::uint32_t>(state, state.gpr[5]))
      *output = address;
    state.gpr[2] = 0;
    if (implementation_->verbose)
      std::fprintf(stderr,
                   "[psprism:memory] allocate-fpl uid=%u output=%08x "
                   "address=%08x\n",
                   state.gpr[4], state.gpr[5], address);
    return;
  }
  if (name == "sceKernelFreeFpl") {
    std::shared_ptr<Implementation::FixedPool> pool;
    {
      std::lock_guard lock(implementation_->objects_mutex);
      const auto found =
          implementation_->fixed_pools.find(static_cast<int>(state.gpr[4]));
      if (found == implementation_->fixed_pools.end())
        return;
      pool = found->second;
    }
    {
      std::lock_guard lock(pool->mutex);
      pool->available.push_back(state.gpr[5]);
    }
    pool->changed.notify_one();
    state.gpr[2] = 0;
    return;
  }
  if (name == "sceKernelDeleteFpl") {
    std::lock_guard lock(implementation_->objects_mutex);
    state.gpr[2] =
        implementation_->fixed_pools.erase(static_cast<int>(state.gpr[4]))
            ? 0U
            : unimplemented;
    return;
  }

  if (name == "sceKernelCreateVpl") {
    const auto size = state.gpr[7];
    if (size == 0) {
      state.gpr[2] = out_of_memory;
      return;
    }
    auto pool = std::make_shared<Implementation::VariablePool>();
    std::lock_guard lock(implementation_->objects_mutex);
    const auto address = implementation_->allocate_heap(size, 8U);
    if (address == 0) {
      state.gpr[2] = out_of_memory;
      return;
    }
    pool->available.push_back({address, size});
    pool->backing = {address, size};
    const auto uid = implementation_->allocate_uid();
    implementation_->variable_pools.emplace(uid, pool);
    state.gpr[2] = static_cast<std::uint32_t>(uid);
    if (implementation_->verbose)
      std::fprintf(stderr,
                   "[psprism:memory] vpl uid=%d size=%u address=%08x\n", uid,
                   size, address);
    return;
  }
  if (name == "sceKernelAllocateVpl" ||
      name == "sceKernelAllocateVplCB" ||
      name == "sceKernelTryAllocateVpl") {
    std::shared_ptr<Implementation::VariablePool> pool;
    {
      std::lock_guard lock(implementation_->objects_mutex);
      const auto found =
          implementation_->variable_pools.find(static_cast<int>(state.gpr[4]));
      if (found == implementation_->variable_pools.end())
        return;
      pool = found->second;
    }
    const auto requested = align_up(state.gpr[5], 8U);
    std::unique_lock lock(pool->mutex);
    const auto allocate = [&]() -> std::uint32_t {
      for (auto i = pool->available.begin(); i != pool->available.end(); ++i) {
        if (i->size < requested)
          continue;
        const auto address = i->address;
        i->address += requested;
        i->size -= requested;
        if (i->size == 0)
          pool->available.erase(i);
        pool->allocated.emplace(address, requested);
        return address;
      }
      return 0;
    };
    auto address = allocate();
    while (address == 0 && name != "sceKernelTryAllocateVpl" &&
           !implementation_->exit_requested) {
      pool->changed.wait(lock);
      address = allocate();
    }
    if (address == 0) {
      state.gpr[2] = out_of_memory;
      return;
    }
    if (auto* output = guest_pointer<std::uint32_t>(state, state.gpr[6]))
      *output = address;
    state.gpr[2] = 0;
    return;
  }
  if (name == "sceKernelFreeVpl") {
    std::shared_ptr<Implementation::VariablePool> pool;
    {
      std::lock_guard lock(implementation_->objects_mutex);
      const auto found =
          implementation_->variable_pools.find(static_cast<int>(state.gpr[4]));
      if (found == implementation_->variable_pools.end())
        return;
      pool = found->second;
    }
    {
      std::lock_guard lock(pool->mutex);
      const auto allocation = pool->allocated.find(state.gpr[5]);
      if (allocation == pool->allocated.end())
        return;
      pool->available.push_back({allocation->first, allocation->second});
      pool->allocated.erase(allocation);
      std::sort(pool->available.begin(), pool->available.end(),
                [](const Implementation::MemoryBlock& left,
                   const Implementation::MemoryBlock& right) {
                  return left.address < right.address;
                });
      for (std::size_t i = 1; i < pool->available.size();) {
        auto& previous = pool->available[i - 1U];
        const auto& current = pool->available[i];
        if (previous.address + previous.size == current.address) {
          previous.size += current.size;
          pool->available.erase(pool->available.begin() + i);
        } else {
          ++i;
        }
      }
    }
    pool->changed.notify_one();
    state.gpr[2] = 0;
    return;
  }
  if (name == "sceKernelDeleteVpl") {
    std::lock_guard lock(implementation_->objects_mutex);
    const auto found = implementation_->variable_pools.find(
        static_cast<int>(state.gpr[4]));
    if (found == implementation_->variable_pools.end())
      return;
    implementation_->free_heap(found->second->backing.address,
                               found->second->backing.size);
    implementation_->variable_pools.erase(found);
    state.gpr[2] = 0U;
    return;
  }

  if (is_one_of(name,
                {"sceKernelGetModuleId", "sceKernelSetCompilerVersion",
                 "sceKernelSetCompiledSdkVersion", "sceKernelPowerTick",
                 "sceKernelDcacheWritebackAll", "sceCtrlSetSamplingMode",
                 "sceCtrlSetSamplingCycle", "sceCtrlSetIdleCancelThreshold",
                 "sceDisplaySetMode", "sceMpegAvcDecodeFlush"})) {
    state.gpr[2] = 0;
    return;
  }
  if (is_one_of(name, {"sceKernelCheckCallback", "sceIoChangeAsyncPriority"})) {
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceDisplaySetFrameBuf") {
    constexpr std::uint32_t width = 480;
    constexpr std::uint32_t height = 272;
    const auto bytes_per_pixel = state.gpr[6] == 3U ? 4U : 2U;
    const auto byte_count =
        static_cast<std::size_t>(state.gpr[5]) * height * bytes_per_pixel;
    if (auto* pixels =
            psprecomp::mapped_address(state, state.gpr[4], byte_count)) {
      const auto frame = implementation_->displayed_frames++;
      if (implementation_->verbose && frame < 4U) {
        std::fprintf(stderr,
                     "[psprism:display] frame=%u address=%08x stride=%u "
                     "format=%u sync=%u\n",
                     frame, state.gpr[4], state.gpr[5], state.gpr[6],
                     state.gpr[7]);
      }
      host::present_frame(pixels, state.gpr[5], width, height, state.gpr[6],
                          state.gpr[4]);
      state.gpr[2] = 0;
    }
    return;
  }
  if (name == "sceGeEdramGetAddr") {
    state.gpr[2] = 0x04000000U;
    return;
  }
  if (name == "sceGeEdramGetSize") {
    state.gpr[2] = 2U * 1024U * 1024U;
    return;
  }
  if (name == "sceGeEdramSetAddrTranslation") {
    state.gpr[2] = state.gpr[4];
    return;
  }
  if (name == "sceGeListEnQueue") {
    static std::atomic<std::uint32_t> next_list{1};
    const auto submission = implementation_->submitted_ge_lists++;
    {
      std::lock_guard graphics_lock(implementation_->graphics.mutex);
      auto& graphics = implementation_->graphics;
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
          if (implementation_->verbose && submission < 2U &&
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
              }
            }
            if (index_type == 0)
              graphics.vertex_address += static_cast<std::uint32_t>(
                  static_cast<std::size_t>(vertex_count) * layout.stride);
            else
              graphics.index_address +=
                  static_cast<std::uint32_t>(index_byte_count);
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
          if (implementation_->verbose && submission == 0U)
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
          if (implementation_->verbose && submission == 0U)
            std::fprintf(stderr, "[psprism:ge] return target=%08x\n",
                         program_counter);
          continue;
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
      if (implementation_->verbose && submission < 8U) {
        std::fprintf(stderr,
                     "[psprism:ge] list=%u address=%08x stall=%08x words=%u "
                     "prims=%u ended=%u commands=",
                     submission, state.gpr[4], state.gpr[5], words, primitives,
                     ended ? 1U : 0U);
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
    state.gpr[2] = next_list++;
    return;
  }
  if (is_one_of(name, {"sceGeListSync", "sceGeContinue"})) {
    state.gpr[2] = 0U;
    return;
  }
  if (is_one_of(name, {"sceKernelExitGame", "sceKernelExitThread"})) {
    if (name == "sceKernelExitGame") {
      implementation_->exit_requested = true;
      host::request_frontend_exit();
      std::lock_guard lock(implementation_->objects_mutex);
      for (const auto& [uid, semaphore] : implementation_->semaphores) {
        static_cast<void>(uid);
        semaphore->changed.notify_all();
      }
      for (const auto& [uid, event] : implementation_->event_flags) {
        static_cast<void>(uid);
        event->changed.notify_all();
      }
      for (const auto& [uid, pool] : implementation_->fixed_pools) {
        static_cast<void>(uid);
        pool->changed.notify_all();
      }
    }
    state.gpr[2] = 0;
    state.stop_reason = psprecomp::StopReason::returned;
    return;
  }
  if (name == "sceKernelSleepThreadCB") {
    host::sleep_microseconds(1000U);
    state.gpr[2] = 0;
    return;
  }
  if (name == "sceKernelPrintf") {
    if (implementation_->verbose)
      if (const auto* format = guest_string(state, state.gpr[4]))
        std::fprintf(stderr, "[guest] %s", format);
    state.gpr[2] = 0;
    return;
  }
  if (is_one_of(name,
                {"sceKernelChangeThreadPriority", "sceKernelPowerLock",
                 "sceKernelPowerUnlock", "sceKernelRegisterExitCallback"})) {
    state.gpr[2] = 0;
    return;
  }
  if (name == "sceKernelCreateCallback") {
    std::lock_guard lock(implementation_->objects_mutex);
    const auto uid = implementation_->allocate_uid();
    implementation_->callbacks.emplace(
        uid, Implementation::Callback{state.gpr[5], state.gpr[6]});
    state.gpr[2] = static_cast<std::uint32_t>(uid);
    return;
  }
  if (name == "sceKernelDeleteCallback") {
    std::lock_guard lock(implementation_->objects_mutex);
    state.gpr[2] =
        implementation_->callbacks.erase(static_cast<int>(state.gpr[4]))
            ? 0U
            : unimplemented;
    return;
  }
  if (name == "sceUmdRegisterUMDCallBack") {
    notify_callback(static_cast<int>(state.gpr[4]), 0x32U);
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceIoDevctl") {
    const auto* device = guest_string(state, state.gpr[4]);
    if (device != nullptr && std::string_view(device) == "fatms0:" &&
        state.gpr[5] == 0x02415821U) {
      if (const auto* uid = guest_pointer<std::uint32_t>(state, state.gpr[6]))
        notify_callback(static_cast<int>(*uid), 1U);
      state.gpr[2] = 0U;
    }
    return;
  }
  if (name == "sceKernelGetSystemTimeWide") {
    const auto value = implementation_->elapsed_microseconds();
    state.gpr[2] = static_cast<std::uint32_t>(value);
    state.gpr[3] = static_cast<std::uint32_t>(value >> 32U);
    return;
  }
  if (name == "sceKernelLibcClock") {
    state.gpr[2] =
        static_cast<std::uint32_t>(implementation_->elapsed_microseconds());
    return;
  }
  if (name == "sceKernelLibcGettimeofday") {
    if (auto* destination =
            psprecomp::mapped_address(state, state.gpr[4], 8U)) {
      const auto elapsed = implementation_->elapsed_microseconds();
      const auto seconds = static_cast<std::uint32_t>(
          implementation_->start_unix_seconds + elapsed / 1000000U);
      const auto microseconds = static_cast<std::uint32_t>(elapsed % 1000000U);
      std::memcpy(destination, &seconds, sizeof(seconds));
      std::memcpy(destination + 4U, &microseconds, sizeof(microseconds));
    } else if (state.gpr[4] != 0) {
      state.gpr[2] = 0U;
      return;
    }
    if (auto* timezone = psprecomp::mapped_address(state, state.gpr[5], 8U))
      std::memset(timezone, 0, 8U);
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceKernelLibcTime") {
    const auto seconds = static_cast<std::uint32_t>(host::unix_seconds());
    if (auto* destination = guest_pointer<std::uint32_t>(state, state.gpr[4])) {
      *destination = seconds;
    }
    state.gpr[2] = seconds;
    return;
  }
  if (name == "sceKernelGetSystemTimeLow") {
    state.gpr[2] =
        static_cast<std::uint32_t>(implementation_->elapsed_microseconds());
    return;
  }
  if (name == "sceKernelVolatileMemTryLock" ||
      name == "sceKernelVolatileMemLock") {
    constexpr std::uint32_t volatile_address = 0x08400000U;
    constexpr std::uint32_t volatile_size = 4U * 1024U * 1024U;
    if (auto* output = guest_pointer<std::uint32_t>(state, state.gpr[5]))
      *output = volatile_address;
    if (auto* output = guest_pointer<std::uint32_t>(state, state.gpr[6]))
      *output = volatile_size;
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceKernelVolatileMemUnlock" ||
      name == "sceUtilityLoadModule" || name == "sceUtilityUnloadModule" ||
      name == "sceKernelChangeCurrentThreadAttr" ||
      name == "sceKernelCpuResumeIntr" || name == "sceKernelSetGPO" ||
      name == "sceGeListUpdateStallAddr" ||
      name == "scePowerRegisterCallback") {
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceKernelCpuSuspendIntr") {
    state.gpr[2] = 1U;
    return;
  }
  if (name == "sceKernelIsCpuIntrEnable") {
    state.gpr[2] = 1U;
    return;
  }
  if (name == "sceKernelStdout") {
    state.gpr[2] = 1U;
    return;
  }
  if (name == "sceKernelGetModuleIdByAddress") {
    state.gpr[2] = 1U;
    return;
  }
  if (name == "sceKernelGetGPI") {
    state.gpr[2] = 0U;
    return;
  }
  if (name == "scePowerGetCpuClockFrequencyInt") {
    state.gpr[2] = 333U;
    return;
  }
  if (name == "sceUtilityGetSystemParamInt") {
    std::uint32_t value{};
    switch (state.gpr[4]) {
    case 2U: value = 0U; break;
    case 3U: value = 1U; break;
    case 4U: value = 2U; break;
    case 5U: value = 0U; break;
    case 6U: value = 0U; break;
    case 7U: value = 0U; break;
    case 8U: value = 1U; break;
    case 9U: value = 1U; break;
    case 10U: value = 0U; break;
    default:
      state.gpr[2] = 0x80110103U;
      return;
    }
    if (auto* output = guest_pointer<std::uint32_t>(state, state.gpr[5])) {
      *output = value;
      state.gpr[2] = 0U;
    }
    return;
  }
  if (name == "sceUtilitySavedataInitStart") {
    constexpr std::size_t minimum_parameter_size = 128U;
    auto* parameters =
        psprecomp::mapped_address(state, state.gpr[4], minimum_parameter_size);
    if (parameters == nullptr) {
      state.gpr[2] = 0x80110388U;
      return;
    }
    std::uint32_t expected{};
    if (!implementation_->active_utility.compare_exchange_strong(expected, 1U)) {
      state.gpr[2] = utility_busy;
      return;
    }
    std::uint32_t mode{};
    std::memcpy(&mode, parameters + 48U, sizeof(mode));
    char game_name[14]{};
    char save_name[21]{};
    char file_name[14]{};
    std::memcpy(game_name, parameters + 60U, 13U);
    std::memcpy(save_name, parameters + 76U, 20U);
    std::memcpy(file_name, parameters + 100U, 13U);
    implementation_->savedata_parameters = state.gpr[4];
    implementation_->savedata_operation_complete = false;
    implementation_->savedata_dialog_presented = false;
    implementation_->savedata_names.clear();
    implementation_->savedata_dialog_id = implementation_->next_dialog_id++;
    implementation_->savedata_status = 1U;

    std::uint32_t name_list_address{};
    std::memcpy(&name_list_address, parameters + 96U,
                sizeof(name_list_address));
    if (name_list_address != 0U) {
      for (std::size_t index = 0; index < 128U; ++index) {
        const auto* entry = psprecomp::mapped_address(
            state, name_list_address + static_cast<std::uint32_t>(index * 20U),
            20U);
        if (entry == nullptr) break;
        const auto value = fixed_string(entry, 20U);
        if (value.empty()) break;
        implementation_->savedata_names.push_back(value);
      }
    }
    if (implementation_->savedata_names.empty() && save_name[0] != '\0')
      implementation_->savedata_names.emplace_back(save_name);

    if (mode == 7U && implementation_->savedata_names.empty()) {
      const auto root = implementation_->configuration.writable_root / "PSP" /
                        "SAVEDATA";
      std::error_code directory_error;
      for (const auto& entry :
           std::filesystem::directory_iterator(root, directory_error)) {
        const auto value = entry.path().filename().string();
        if (entry.is_directory() && value.starts_with(game_name))
          implementation_->savedata_names.push_back(
              value.substr(std::strlen(game_name)));
      }
    }

    const bool interactive = mode >= 2U && mode <= 7U;
    if (interactive) {
      std::uint32_t language{};
      std::uint32_t button_swap{};
      std::memcpy(&language, parameters + 4U, sizeof(language));
      std::memcpy(&button_swap, parameters + 8U, sizeof(button_swap));
      const auto text = utility_text(language);
      host::DialogModel dialog;
      dialog.id = implementation_->savedata_dialog_id;
      dialog.kind = mode == 2U || mode == 4U
                        ? host::DialogKind::savedata_load
                    : mode == 6U || mode == 7U
                        ? host::DialogKind::savedata_delete
                        : host::DialogKind::savedata_save;
      dialog.title = dialog.kind == host::DialogKind::savedata_load
                         ? text.load
                     : dialog.kind == host::DialogKind::savedata_delete
                         ? text.remove
                         : text.save;
      dialog.accept_label = dialog.kind == host::DialogKind::savedata_load
                                ? text.load
                            : dialog.kind == host::DialogKind::savedata_delete
                                ? text.remove
                                : text.save;
      dialog.cancel_label = text.back;
      dialog.confirm_with_cross = button_swap != 0U;
      const auto save_root = implementation_->configuration.writable_root /
                             "PSP" / "SAVEDATA";
      for (const auto& entry_name : implementation_->savedata_names) {
        host::DialogItem item;
        const auto directory =
            save_root / (std::string(game_name) + entry_name);
        std::error_code item_error;
        item.empty = !std::filesystem::is_directory(directory, item_error);
        if (item.empty) {
          item.title = text.empty;
          item.detail = entry_name;
        } else {
          const auto sfo = read_binary_file(directory / "PARAM.SFO", 1U << 20U);
          item.title = utility::sfo_string(sfo, "TITLE");
          item.subtitle = utility::sfo_string(sfo, "SAVEDATA_TITLE");
          if (item.title.empty()) item.title = item.subtitle;
          if (item.title.empty()) item.title = entry_name;
          item.detail = utility::sfo_string(sfo, "SAVEDATA_DETAIL");
          item.timestamp = file_timestamp(directory);
          item.size = directory_size_label(directory);
          item.icon_png = read_binary_file(directory / "ICON0.PNG", 4U << 20U);
          item.preview_png = read_binary_file(directory / "PIC1.PNG", 4U << 20U);
        }
        dialog.items.push_back(std::move(item));
      }
      if (dialog.items.empty() &&
          dialog.kind == host::DialogKind::savedata_save) {
        implementation_->savedata_names.emplace_back(save_name);
        dialog.items.push_back(
            {text.empty, {}, save_name, {}, {}, {}, {}, true});
      }
      std::uint32_t focus{};
      if (auto* complete = psprecomp::mapped_address(
              state, state.gpr[4], 1484U))
        std::memcpy(&focus, complete + 1480U, sizeof(focus));
      if (!dialog.items.empty()) {
        if (focus == 2U || focus == 6U || focus == 8U)
          dialog.selected_item = dialog.items.size() - 1U;
        if (focus == 3U || focus == 4U) {
          std::error_code newest_error;
          std::filesystem::file_time_type chosen{};
          for (std::size_t index = 0; index < dialog.items.size(); ++index) {
            const auto directory = save_root /
                (std::string(game_name) + implementation_->savedata_names[index]);
            const auto time = std::filesystem::last_write_time(directory,
                                                               newest_error);
            if (!newest_error && (index == 0U ||
                (focus == 3U ? time > chosen : time < chosen))) {
              chosen = time;
              dialog.selected_item = index;
            }
            newest_error.clear();
          }
        }
        if (focus == 7U || focus == 8U) {
          for (std::size_t index = 0; index < dialog.items.size(); ++index) {
            const auto candidate = focus == 7U ? index
                : dialog.items.size() - 1U - index;
            if (dialog.items[candidate].empty) {
              dialog.selected_item = candidate;
              break;
            }
          }
        }
      }
      host::present_dialog(std::move(dialog));
      implementation_->savedata_dialog_presented = true;
    }
    if (implementation_->verbose)
      std::fprintf(
          stderr,
          "[psprism:savedata] init mode=%u game=%s save=%s file=%s\n", mode,
          game_name, save_name, file_name);
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceUtilitySavedataGetStatus") {
    const auto status = implementation_->savedata_status.load();
    state.gpr[2] = status;
    if (status == 1U)
      implementation_->savedata_status = 2U;
    else if (status == 4U) {
      implementation_->savedata_status = 0U;
      implementation_->active_utility = 0U;
    }
    return;
  }
  if (name == "sceUtilitySavedataUpdate") {
    if (implementation_->savedata_status == 2U &&
        !implementation_->savedata_operation_complete) {
      if (implementation_->savedata_dialog_presented) {
        const auto result = host::poll_dialog_result(
            implementation_->savedata_dialog_id);
        if (!result) {
          state.gpr[2] = 0U;
          return;
        }
        if (auto* parameters = psprecomp::mapped_address(
                state, implementation_->savedata_parameters, 128U)) {
          if (result->cancelled) {
            std::memcpy(parameters + 28U, &utility_cancelled,
                        sizeof(utility_cancelled));
            implementation_->savedata_operation_complete = true;
            implementation_->savedata_status = 3U;
            state.gpr[2] = 0U;
            return;
          }
          if (result->selected_item < implementation_->savedata_names.size()) {
            std::memset(parameters + 76U, 0, 20U);
            const auto& selected = implementation_->savedata_names[
                result->selected_item];
            std::memcpy(parameters + 76U, selected.data(),
                        std::min<std::size_t>(selected.size(), 19U));
          }
        }
      }
      implementation_->savedata_operation_complete = true;
      implementation_->savedata_status = 3U;
      if (auto* parameters = psprecomp::mapped_address(
              state, implementation_->savedata_parameters, 1536U)) {
        const std::uint32_t success{};
        std::memcpy(parameters + 28U, &success, sizeof(success));
        std::uint32_t mode{};
        std::memcpy(&mode, parameters + 48U, sizeof(mode));
        char game_name[14]{};
        char save_name[21]{};
        char file_name[14]{};
        std::memcpy(game_name, parameters + 60U, 13U);
        std::memcpy(save_name, parameters + 76U, 20U);
        std::memcpy(file_name, parameters + 100U, 13U);
        const auto safe_component = [](const char* value) {
          return value[0] != '\0' && std::strcmp(value, ".") != 0 &&
                 std::strcmp(value, "..") != 0 &&
                 std::strchr(value, '/') == nullptr &&
                 std::strchr(value, '\\') == nullptr;
        };
        const auto save_directory =
            implementation_->configuration.writable_root / "PSP" / "SAVEDATA" /
            (std::string(game_name) + save_name);
        const auto save_file = save_directory / file_name;
        const bool valid_directory = safe_component(game_name) &&
                                     safe_component(save_name);
        const bool valid_path = valid_directory && safe_component(file_name);
        const bool load_mode = mode == 0U || mode == 2U || mode == 4U ||
                               mode == 15U || mode == 16U;
        const bool save_mode = mode == 1U || mode == 3U || mode == 5U ||
                               mode == 13U || mode == 14U || mode == 17U ||
                               mode == 18U;
        const bool delete_mode = mode == 6U || mode == 7U || mode == 9U ||
                                 mode == 10U || mode == 19U || mode == 20U ||
                                 mode == 21U;
        if (load_mode) {
          std::uint32_t data_address{};
          std::uint32_t buffer_size{};
          std::memcpy(&data_address, parameters + 116U,
                      sizeof(data_address));
          std::memcpy(&buffer_size, parameters + 120U,
                      sizeof(buffer_size));
          std::error_code file_error;
          const auto file_size = valid_path
                                     ? std::filesystem::file_size(save_file,
                                                                  file_error)
                                     : 0U;
          auto* destination = psprecomp::mapped_address(
              state, data_address,
              static_cast<std::size_t>(std::min<std::uintmax_t>(
                  file_size, static_cast<std::uintmax_t>(buffer_size))));
          if (!valid_path || file_error || destination == nullptr) {
            const std::uint32_t no_data =
                mode == 15U || mode == 16U ? 0x80110327U : 0x80110307U;
            std::memcpy(parameters + 28U, &no_data, sizeof(no_data));
          } else {
            const auto read_size = static_cast<std::uint32_t>(
                std::min<std::uintmax_t>(file_size, buffer_size));
            std::ifstream input(save_file, std::ios::binary);
            input.read(reinterpret_cast<char*>(destination), read_size);
            std::memcpy(parameters + 124U, &read_size, sizeof(read_size));
            if (!input) {
              const std::uint32_t access_error = 0x80110305U;
              std::memcpy(parameters + 28U, &access_error,
                          sizeof(access_error));
            } else {
              const auto read_guest_file = [&](std::size_t offset,
                                               const char* source_name) {
                std::uint32_t address{};
                std::uint32_t capacity{};
                std::memcpy(&address, parameters + offset, sizeof(address));
                std::memcpy(&capacity, parameters + offset + 4U,
                            sizeof(capacity));
                const auto bytes = read_binary_file(save_directory / source_name);
                const auto copied = static_cast<std::uint32_t>(
                    std::min<std::size_t>(bytes.size(), capacity));
                auto* output = psprecomp::mapped_address(state, address, copied);
                if (copied != 0U && output != nullptr)
                  std::memcpy(output, bytes.data(), copied);
                std::memcpy(parameters + offset + 8U, &copied,
                            sizeof(copied));
              };
              read_guest_file(1412U, "ICON0.PNG");
              read_guest_file(1428U, "ICON1.PMF");
              read_guest_file(1444U, "PIC1.PNG");
              read_guest_file(1460U, "SND0.AT3");
              const auto sfo = read_binary_file(save_directory / "PARAM.SFO",
                                                1U << 20U);
              const auto copy_metadata = [&](std::size_t offset,
                                             std::size_t capacity,
                                             std::string_view key) {
                const auto value = utility::sfo_string(sfo, key);
                std::memset(parameters + offset, 0, capacity);
                std::memcpy(parameters + offset, value.data(),
                            std::min(value.size(), capacity - 1U));
              };
              copy_metadata(128U, 128U, "TITLE");
              copy_metadata(256U, 128U, "SAVEDATA_TITLE");
              copy_metadata(384U, 1024U, "SAVEDATA_DETAIL");
            }
          }
        } else if (save_mode) {
          std::uint32_t data_address{};
          std::uint32_t data_size{};
          std::memcpy(&data_address, parameters + 116U,
                      sizeof(data_address));
          std::memcpy(&data_size, parameters + 124U, sizeof(data_size));
          const auto* source =
              psprecomp::mapped_address(state, data_address, data_size);
          if (!valid_path || source == nullptr) {
            const std::uint32_t parameter_error = 0x80110388U;
            std::memcpy(parameters + 28U, &parameter_error,
                        sizeof(parameter_error));
          } else {
            const auto staging = save_directory.string() + ".psprism.tmp";
            const auto backup = save_directory.string() + ".psprism.backup";
            std::error_code directory_error;
            std::filesystem::remove_all(staging, directory_error);
            directory_error.clear();
            std::filesystem::create_directories(staging, directory_error);
            std::ofstream output(std::filesystem::path(staging) / file_name,
                                 std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(source), data_size);
            const auto write_guest_file = [&](std::size_t offset,
                                              const char* destination) {
              std::uint32_t address{};
              std::uint32_t size{};
              std::memcpy(&address, parameters + offset, sizeof(address));
              std::memcpy(&size, parameters + offset + 8U, sizeof(size));
              const auto* bytes =
                  psprecomp::mapped_address(state, address, size);
              if (size == 0U) return true;
              if (bytes == nullptr) return false;
              std::ofstream asset(std::filesystem::path(staging) / destination,
                                  std::ios::binary | std::ios::trunc);
              asset.write(reinterpret_cast<const char*>(bytes), size);
              return static_cast<bool>(asset);
            };
            const bool assets_ok =
                write_guest_file(1412U, "ICON0.PNG") &&
                write_guest_file(1428U, "ICON1.PMF") &&
                write_guest_file(1444U, "PIC1.PNG") &&
                write_guest_file(1460U, "SND0.AT3");
            const auto sfo = utility::make_savedata_sfo(
                fixed_string(parameters + 128U, 128U),
                fixed_string(parameters + 256U, 128U),
                fixed_string(parameters + 384U, 1024U));
            std::ofstream sfo_output(
                std::filesystem::path(staging) / "PARAM.SFO",
                std::ios::binary | std::ios::trunc);
            sfo_output.write(reinterpret_cast<const char*>(sfo.data()),
                             sfo.size());
            output.close();
            sfo_output.close();
            if (directory_error || !output || !sfo_output || !assets_ok) {
              const std::uint32_t access_error = 0x80110385U;
              std::memcpy(parameters + 28U, &access_error,
                          sizeof(access_error));
              std::filesystem::remove_all(staging, directory_error);
            } else {
              std::filesystem::remove_all(backup, directory_error);
              directory_error.clear();
              const bool had_existing =
                  std::filesystem::exists(save_directory, directory_error);
              if (had_existing)
                std::filesystem::rename(save_directory, backup,
                                        directory_error);
              if (!directory_error)
                std::filesystem::rename(staging, save_directory,
                                        directory_error);
              if (directory_error) {
                std::error_code rollback_error;
                std::filesystem::remove_all(save_directory, rollback_error);
                if (had_existing)
                  std::filesystem::rename(backup, save_directory,
                                          rollback_error);
                const std::uint32_t access_error = 0x80110385U;
                std::memcpy(parameters + 28U, &access_error,
                            sizeof(access_error));
              } else {
                std::filesystem::remove_all(backup, directory_error);
              }
            }
          }
        } else if (delete_mode) {
          std::error_code delete_error;
          if (mode == 7U) {
            const auto save_root = implementation_->configuration.writable_root /
                                   "PSP" / "SAVEDATA";
            for (const auto& selected : implementation_->savedata_names)
              std::filesystem::remove_all(
                  save_root / (std::string(game_name) + selected),
                  delete_error);
          } else if (valid_directory) {
            std::filesystem::remove_all(save_directory, delete_error);
          }
          if (delete_error) {
            const std::uint32_t access_error = 0x80110385U;
            std::memcpy(parameters + 28U, &access_error,
                        sizeof(access_error));
          }
        }
        if (mode == 8U) {
          std::uint32_t free_info{};
          std::uint32_t data_info{};
          std::uint32_t utility_info{};
          std::memcpy(&free_info, parameters + 1488U, sizeof(free_info));
          std::memcpy(&data_info, parameters + 1492U, sizeof(data_info));
          std::memcpy(&utility_info, parameters + 1496U, sizeof(utility_info));
          std::error_code space_error;
          const auto space = std::filesystem::space(
              implementation_->configuration.writable_root, space_error);
          const auto available =
              space_error ? 1024ULL * 1024ULL * 1024ULL : space.available;
          const auto free_kb = static_cast<std::uint32_t>(
              std::min<std::uintmax_t>(available / 1024U, 0x7fffffffU));
          if (auto* output = psprecomp::mapped_address(state, free_info, 20U)) {
            std::memset(output, 0, 20U);
            const std::uint32_t cluster_size = 32768U;
            const auto free_clusters = free_kb / 32U;
            std::memcpy(output, &cluster_size, sizeof(cluster_size));
            std::memcpy(output + 4U, &free_clusters, sizeof(free_clusters));
            std::memcpy(output + 8U, &free_kb, sizeof(free_kb));
            std::snprintf(reinterpret_cast<char*>(output + 12U), 8U, "%uMB",
                          free_kb / 1024U);
          }
          if (auto* output = psprecomp::mapped_address(state, data_info, 64U)) {
            std::memset(output + 36U, 0, 28U);
            char requested_game[14]{};
            char requested_save[21]{};
            std::memcpy(requested_game, output, 13U);
            std::memcpy(requested_save, output + 16U, 20U);
            const auto requested_directory =
                implementation_->configuration.writable_root / "PSP" /
                "SAVEDATA" /
                (std::string(requested_game) + requested_save);
            std::error_code directory_error;
            if (!std::filesystem::is_directory(requested_directory,
                                               directory_error)) {
              const std::uint32_t no_data = 0x801103c7U;
              std::memcpy(parameters + 28U, &no_data, sizeof(no_data));
            }
          }
          if (auto* output =
                  psprecomp::mapped_address(state, utility_info, 28U)) {
            std::uint32_t data_size{};
            std::memcpy(&data_size, parameters + 124U, sizeof(data_size));
            std::uint64_t requested_bytes = 2U * 32768U;
            const auto add_file = [&](std::uint32_t size) {
              if (size != 0U)
                requested_bytes += (static_cast<std::uint64_t>(size) +
                                    32767U) & ~32767ULL;
            };
            add_file(data_size + (data_size == 0U ? 0U : 16U));
            for (const auto offset : {1420U, 1436U, 1452U, 1468U}) {
              std::uint32_t size{};
              std::memcpy(&size, parameters + offset, sizeof(size));
              add_file(size);
            }
            const auto used_kb = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(requested_bytes / 1024U,
                                        0x7fffffffU));
            const auto used_clusters = used_kb / 32U;
            std::memset(output, 0, 28U);
            std::memcpy(output, &used_clusters, sizeof(used_clusters));
            std::memcpy(output + 4U, &used_kb, sizeof(used_kb));
            std::snprintf(reinterpret_cast<char*>(output + 8U), 8U, "%u KB",
                          used_kb);
            std::memcpy(output + 16U, &used_kb, sizeof(used_kb));
            std::snprintf(reinterpret_cast<char*>(output + 20U), 8U, "%u KB",
                          used_kb);
          }
          if (implementation_->verbose)
            std::fprintf(
                stderr, "[psprism:savedata] memory-stick free=%uKB\n",
                free_kb);
        }
        if (mode == 11U) {
          std::uint32_t list_address{};
          std::memcpy(&list_address, parameters + 1524U,
                      sizeof(list_address));
          if (auto* list = psprecomp::mapped_address(state, list_address, 12U)) {
            std::uint32_t maximum{};
            std::uint32_t entries_address{};
            std::memcpy(&maximum, list, sizeof(maximum));
            std::memcpy(&entries_address, list + 8U, sizeof(entries_address));
            maximum = std::min(maximum, 1024U);
            std::uint32_t count{};
            const auto root = implementation_->configuration.writable_root /
                              "PSP" / "SAVEDATA";
            std::error_code list_error;
            for (const auto& entry :
                 std::filesystem::directory_iterator(root, list_error)) {
              const auto full_name = entry.path().filename().string();
              if (!entry.is_directory() || !full_name.starts_with(game_name) ||
                  count >= maximum)
                continue;
              auto* output = psprecomp::mapped_address(
                  state, entries_address + count * 72U, 72U);
              if (output == nullptr) break;
              std::memset(output, 0, 72U);
              const std::uint32_t directory_mode = 0x11ffU;
              std::memcpy(output, &directory_mode, sizeof(directory_mode));
              const auto suffix = full_name.substr(std::strlen(game_name));
              std::memcpy(output + 52U, suffix.data(),
                          std::min<std::size_t>(suffix.size(), 19U));
              ++count;
            }
            std::memcpy(list + 4U, &count, sizeof(count));
          }
        }
        if (mode == 12U) {
          std::uint32_t list_address{};
          std::memcpy(&list_address, parameters + 1528U,
                      sizeof(list_address));
          if (auto* list = psprecomp::mapped_address(state, list_address, 36U)) {
            std::uint32_t max_normal{};
            std::uint32_t max_system{};
            std::uint32_t normal_address{};
            std::uint32_t system_address{};
            std::memcpy(&max_normal, list + 4U, sizeof(max_normal));
            std::memcpy(&max_system, list + 8U, sizeof(max_system));
            std::memcpy(&normal_address, list + 28U, sizeof(normal_address));
            std::memcpy(&system_address, list + 32U, sizeof(system_address));
            max_normal = std::min(max_normal, 1024U);
            max_system = std::min(max_system, 1024U);
            std::uint32_t normal_count{};
            std::uint32_t system_count{};
            std::error_code file_error;
            for (const auto& entry :
                 std::filesystem::directory_iterator(save_directory,
                                                     file_error)) {
              if (!entry.is_regular_file()) continue;
              const auto filename = entry.path().filename().string();
              const bool system = is_one_of(
                  filename, {"PARAM.SFO", "ICON0.PNG", "ICON1.PMF",
                             "PIC1.PNG", "SND0.AT3"});
              auto& count = system ? system_count : normal_count;
              const auto maximum = system ? max_system : max_normal;
              const auto address = system ? system_address : normal_address;
              if (count >= maximum) continue;
              auto* output = psprecomp::mapped_address(
                  state, address + count * 80U, 80U);
              if (output == nullptr) continue;
              std::memset(output, 0, 80U);
              const std::uint32_t file_mode = 0x21ffU;
              const auto size = static_cast<std::uint64_t>(entry.file_size());
              std::memcpy(output, &file_mode, sizeof(file_mode));
              std::memcpy(output + 8U, &size, sizeof(size));
              std::memcpy(output + 64U, filename.data(),
                          std::min<std::size_t>(filename.size(), 15U));
              ++count;
            }
            std::memcpy(list + 16U, &normal_count, sizeof(normal_count));
            std::memcpy(list + 20U, &system_count, sizeof(system_count));
          }
        }
        if (mode == 22U) {
          std::uint32_t info_address{};
          std::memcpy(&info_address, parameters + 1532U,
                      sizeof(info_address));
          if (auto* info = psprecomp::mapped_address(state, info_address, 60U)) {
            std::uint64_t used_bytes{};
            std::error_code size_error;
            for (const auto& entry : std::filesystem::directory_iterator(
                     save_directory, size_error)) {
              if (entry.is_regular_file()) used_bytes += entry.file_size();
            }
            const auto used_kb = static_cast<std::uint32_t>(
                std::min<std::uint64_t>((used_bytes + 1023U) / 1024U,
                                        0x7fffffffU));
            const auto space = std::filesystem::space(
                implementation_->configuration.writable_root, size_error);
            const auto free_kb = size_error ? 0U :
                static_cast<std::uint32_t>(std::min<std::uintmax_t>(
                    space.available / 1024U, 0x7fffffffU));
            std::memcpy(info + 24U, &free_kb, sizeof(free_kb));
            std::snprintf(reinterpret_cast<char*>(info + 28U), 8U, "%u KB",
                          free_kb);
            std::memcpy(info + 36U, &used_kb, sizeof(used_kb));
            std::snprintf(reinterpret_cast<char*>(info + 40U), 8U, "%u KB",
                          used_kb);
            std::memcpy(info + 48U, &used_kb, sizeof(used_kb));
            std::snprintf(reinterpret_cast<char*>(info + 52U), 8U, "%u KB",
                          used_kb);
          }
        }
      }
    }
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceUtilitySavedataShutdownStart") {
    host::dismiss_dialog(implementation_->savedata_dialog_id);
    implementation_->savedata_status = 4U;
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceUtilityMsgDialogInitStart") {
    auto* parameters = psprecomp::mapped_address(state, state.gpr[4], 572U);
    if (parameters == nullptr) {
      state.gpr[2] = 0x80110002U;
      return;
    }
    std::uint32_t expected{};
    if (!implementation_->active_utility.compare_exchange_strong(expected, 2U)) {
      state.gpr[2] = utility_busy;
      return;
    }
    implementation_->message_dialog_parameters = state.gpr[4];
    implementation_->message_dialog_id = implementation_->next_dialog_id++;
    implementation_->message_dialog_status = 1U;
    std::uint32_t size{};
    std::uint32_t language{};
    std::uint32_t button_swap{};
    std::uint32_t mode{};
    std::uint32_t error_value{};
    std::memcpy(&size, parameters, sizeof(size));
    std::memcpy(&language, parameters + 4U, sizeof(language));
    std::memcpy(&button_swap, parameters + 8U, sizeof(button_swap));
    std::memcpy(&mode, parameters + 52U, sizeof(mode));
    std::memcpy(&error_value, parameters + 56U, sizeof(error_value));
    std::uint32_t options{};
    if (size >= 576U) std::memcpy(&options, parameters + 572U, sizeof(options));
    const auto text = utility_text(language);
    host::DialogModel dialog;
    dialog.id = implementation_->message_dialog_id;
    dialog.kind = host::DialogKind::message;
    dialog.title = text.message;
    if (mode == 0U) {
      char formatted[64]{};
      std::snprintf(formatted, sizeof(formatted), "Error 0x%08X", error_value);
      dialog.message = formatted;
    } else {
      dialog.message = fixed_string(parameters + 60U, 512U);
    }
    dialog.confirm_with_cross = button_swap != 0U;
    dialog.yes_no = (options & 0x10U) != 0U;
    dialog.default_no = (options & 0x100U) != 0U;
    dialog.accept_label = text.accept;
    dialog.cancel_label = text.back;
    dialog.yes_label = text.yes;
    dialog.no_label = text.no;
    host::present_dialog(std::move(dialog));
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceUtilityMsgDialogGetStatus") {
    const auto status = implementation_->message_dialog_status.load();
    state.gpr[2] = status;
    if (status == 1U)
      implementation_->message_dialog_status = 2U;
    else if (status == 4U) {
      implementation_->message_dialog_status = 0U;
      implementation_->active_utility = 0U;
    }
    return;
  }
  if (name == "sceUtilityMsgDialogUpdate") {
    if (implementation_->message_dialog_status == 2U) {
      const auto dialog_result = host::poll_dialog_result(
          implementation_->message_dialog_id);
      if (!dialog_result) {
        state.gpr[2] = 0U;
        return;
      }
      if (auto* parameters = psprecomp::mapped_address(
              state, implementation_->message_dialog_parameters, 572U)) {
        std::uint32_t size{};
        std::memcpy(&size, parameters, sizeof(size));
        const std::uint32_t success{};
        const auto common_result = dialog_result->cancelled
                                       ? utility_cancelled
                                       : success;
        std::memcpy(parameters + 28U, &common_result, sizeof(common_result));
        std::memcpy(parameters + 48U, &success, sizeof(success));
        if (size >= 580U) {
          const std::uint32_t button = dialog_result->cancelled
                                           ? 3U
                                       : dialog_result->affirmative ? 1U : 2U;
          if (auto* complete = psprecomp::mapped_address(
                  state, implementation_->message_dialog_parameters, size))
            std::memcpy(complete + 576U, &button, sizeof(button));
        }
      }
      implementation_->message_dialog_status = 3U;
    }
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceUtilityMsgDialogShutdownStart") {
    host::dismiss_dialog(implementation_->message_dialog_id);
    implementation_->message_dialog_status = 4U;
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceUtilityMsgDialogAbort") {
    if (implementation_->message_dialog_status == 0U) {
      state.gpr[2] = 0x80110002U;
      return;
    }
    host::dismiss_dialog(implementation_->message_dialog_id);
    if (auto* parameters = psprecomp::mapped_address(
            state, implementation_->message_dialog_parameters, 52U)) {
      std::memcpy(parameters + 28U, &utility_cancelled,
                  sizeof(utility_cancelled));
    }
    implementation_->message_dialog_status = 3U;
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceUtilityOskInitStart") {
    auto* parameters = psprecomp::mapped_address(state, state.gpr[4], 64U);
    if (parameters == nullptr) {
      state.gpr[2] = 0x80110002U;
      return;
    }
    std::uint32_t expected{};
    if (!implementation_->active_utility.compare_exchange_strong(expected, 3U)) {
      state.gpr[2] = utility_busy;
      return;
    }
    std::uint32_t language{};
    std::uint32_t button_swap{};
    std::uint32_t count{};
    std::uint32_t data_address{};
    std::memcpy(&language, parameters + 4U, sizeof(language));
    std::memcpy(&button_swap, parameters + 8U, sizeof(button_swap));
    std::memcpy(&count, parameters + 48U, sizeof(count));
    std::memcpy(&data_address, parameters + 52U, sizeof(data_address));
    if (count == 0U || count > 16U || psprecomp::mapped_address(
            state, data_address, static_cast<std::size_t>(count) * 52U) == nullptr) {
      implementation_->active_utility = 0U;
      state.gpr[2] = 0x80110002U;
      return;
    }
    implementation_->osk_parameters = state.gpr[4];
    implementation_->osk_dialog_id = implementation_->next_dialog_id++;
    implementation_->osk_status = 1U;
    const std::uint32_t local_state = 1U;
    std::memcpy(parameters + 56U, &local_state, sizeof(local_state));
    const auto text = utility_text(language);
    host::DialogModel dialog;
    dialog.id = implementation_->osk_dialog_id;
    dialog.kind = host::DialogKind::osk;
    dialog.title = text.keyboard;
    dialog.confirm_with_cross = button_swap != 0U;
    dialog.accept_label = text.accept;
    dialog.cancel_label = text.back;
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto entry_address = data_address + index * 52U;
      auto* entry = psprecomp::mapped_address(state, entry_address, 52U);
      std::uint32_t input_type{};
      std::uint32_t description{};
      std::uint32_t input{};
      std::uint32_t output_length{};
      std::uint32_t output_limit{};
      std::memcpy(&input_type, entry + 16U, sizeof(input_type));
      std::memcpy(&description, entry + 28U, sizeof(description));
      std::memcpy(&input, entry + 32U, sizeof(input));
      std::memcpy(&output_length, entry + 36U, sizeof(output_length));
      std::memcpy(&output_limit, entry + 48U, sizeof(output_limit));
      host::OskField field;
      field.label = utf16_to_utf8(guest_utf16(state, description));
      field.text = guest_utf16(state, input,
                               std::min<std::uint32_t>(output_length, 4096U));
      field.limit = std::min(output_limit,
                             output_length == 0U ? output_limit
                                                : output_length - 1U);
      field.input_type = input_type;
      dialog.fields.push_back(std::move(field));
    }
    host::present_dialog(std::move(dialog));
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceUtilityOskGetStatus") {
    const auto status = implementation_->osk_status.load();
    state.gpr[2] = status;
    if (status == 1U) {
      implementation_->osk_status = 2U;
      if (auto* parameters = psprecomp::mapped_address(
              state, implementation_->osk_parameters, 60U)) {
        const std::uint32_t local_state = 3U;
        std::memcpy(parameters + 56U, &local_state, sizeof(local_state));
      }
    } else if (status == 4U) {
      implementation_->osk_status = 0U;
      implementation_->active_utility = 0U;
    }
    return;
  }
  if (name == "sceUtilityOskUpdate") {
    if (implementation_->osk_status != 2U) {
      state.gpr[2] = 0U;
      return;
    }
    const auto result = host::poll_dialog_result(implementation_->osk_dialog_id);
    if (!result) {
      state.gpr[2] = 0U;
      return;
    }
    auto* parameters = psprecomp::mapped_address(
        state, implementation_->osk_parameters, 60U);
    if (parameters != nullptr) {
      std::uint32_t count{};
      std::uint32_t data_address{};
      std::memcpy(&count, parameters + 48U, sizeof(count));
      std::memcpy(&data_address, parameters + 52U, sizeof(data_address));
      for (std::uint32_t index = 0;
           index < count && index < result->field_text.size(); ++index) {
        auto* entry = psprecomp::mapped_address(
            state, data_address + index * 52U, 52U);
        if (entry == nullptr) break;
        std::uint32_t output_length{};
        std::uint32_t output_address{};
        std::uint32_t output_limit{};
        std::uint32_t input_address{};
        std::memcpy(&input_address, entry + 32U, sizeof(input_address));
        std::memcpy(&output_length, entry + 36U, sizeof(output_length));
        std::memcpy(&output_address, entry + 40U, sizeof(output_address));
        std::memcpy(&output_limit, entry + 48U, sizeof(output_limit));
        const auto maximum = std::min(output_length,
            output_limit == 0U ? output_length : output_limit + 1U);
        const auto original = guest_utf16(state, input_address, maximum);
        auto* output = psprecomp::mapped_address(
            state, output_address, static_cast<std::size_t>(maximum) * 2U);
        if (output != nullptr && maximum != 0U) {
          const auto length = std::min<std::size_t>(
              result->field_text[index].size(), maximum - 1U);
          std::memset(output, 0, static_cast<std::size_t>(maximum) * 2U);
          std::memcpy(output, result->field_text[index].data(), length * 2U);
        }
        const std::uint32_t field_result = result->cancelled
                                               ? 1U
                                           : original == result->field_text[index]
                                               ? 0U
                                               : 2U;
        std::memcpy(entry + 44U, &field_result, sizeof(field_result));
      }
      const std::uint32_t local_state = 4U;
      std::memcpy(parameters + 56U, &local_state, sizeof(local_state));
      const auto common_result = result->cancelled ? utility_cancelled : 0U;
      std::memcpy(parameters + 28U, &common_result, sizeof(common_result));
    }
    implementation_->osk_status = 3U;
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceUtilityOskShutdownStart") {
    host::dismiss_dialog(implementation_->osk_dialog_id);
    if (auto* parameters = psprecomp::mapped_address(
            state, implementation_->osk_parameters, 60U)) {
      const std::uint32_t local_state = 5U;
      std::memcpy(parameters + 56U, &local_state, sizeof(local_state));
    }
    implementation_->osk_status = 4U;
    state.gpr[2] = 0U;
    return;
  }
  if (is_one_of(name, {"sceKernelDelayThread", "sceKernelDelayThreadCB"})) {
    host::sleep_microseconds(state.gpr[4]);
    state.gpr[2] = 0;
    return;
  }
  if (name == "sceDisplayGetFramePerSec") {
    state.gpr[2] = 0x41efc71cU; // 59.940060f
    state.fpr[0] = state.gpr[2];
    return;
  }
  if (name == "sceDisplayGetVcount") {
    state.gpr[2] = static_cast<std::uint32_t>(
        implementation_->elapsed_microseconds() / 16683U);
    return;
  }
  if (is_one_of(name, {"sceDisplayWaitVblank", "sceDisplayWaitVblankCB",
                       "sceDisplayWaitVblankStart",
                       "sceDisplayWaitVblankStartCB"})) {
    host::present_ge_frame();
    host::sleep_microseconds(16683U);
    state.gpr[2] = 0;
    return;
  }
  if (is_one_of(name, {"sceUmdCheckMedium", "sceUmdWaitDriveStatCB",
                       "sceUmdCancelWaitDriveStat", "sceUmdWaitDriveStat",
                       "sceUmdActivate"})) {
    state.gpr[2] = name == "sceUmdCheckMedium" ? 1U : 0U;
    return;
  }
  if (name == "sceUmdGetDriveStat") {
    state.gpr[2] = 0x32U; // present, ready, readable
    return;
  }
  if (name == "sceKernelReferSystemStatus") {
    constexpr std::uint32_t system_status_size = 28U;
    auto* output = psprecomp::mapped_address(state, state.gpr[4],
                                             system_status_size);
    if (output == nullptr)
      return;
    std::memset(output, 0, system_status_size);
    std::memcpy(output, &system_status_size, sizeof(system_status_size));
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceImposeGetLanguageMode") {
    if (auto* language = guest_pointer<std::uint32_t>(state, state.gpr[4]))
      *language = 1U; // English
    if (auto* button = guest_pointer<std::uint32_t>(state, state.gpr[5]))
      *button = 1U; // Cross confirms
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceImposeSetLanguageMode") {
    state.gpr[2] = 0U;
    return;
  }
  if (name == "sceRtcGetCurrentTick") {
    constexpr std::uint64_t unix_epoch_psp_ticks = 62135596800000000ULL;
    if (auto* output = guest_pointer<std::uint64_t>(state, state.gpr[4])) {
      *output = unix_epoch_psp_ticks +
                implementation_->start_unix_seconds * 1000000ULL +
                implementation_->elapsed_microseconds();
      state.gpr[2] = 0U;
    }
    return;
  }
  if (is_one_of(name,
                {"sceCtrlReadBufferPositive", "sceCtrlPeekBufferPositive"})) {
    constexpr std::size_t pad_size = 16;
    const auto count = state.gpr[5];
    if (!psprecomp::address_ok(state, state.gpr[4], pad_size * count))
      return;
    auto* output =
        psprecomp::mapped_address(state, state.gpr[4], pad_size * count);
    const auto controller = host::controller_state();
    if (implementation_->verbose && controller.buttons != 0)
      std::fprintf(stderr, "[psprism:controller] buttons=%08x\n",
                   controller.buttons);
    std::memset(output, 0, pad_size * count);
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto timestamp =
          static_cast<std::uint32_t>(implementation_->elapsed_microseconds());
      std::memcpy(output + index * pad_size, &timestamp, sizeof(timestamp));
      std::memcpy(output + index * pad_size + 4U, &controller.buttons,
                  sizeof(controller.buttons));
      output[index * pad_size + 8] = controller.analog_x;
      output[index * pad_size + 9] = controller.analog_y;
    }
    state.gpr[2] = count;
    return;
  }
  if (is_one_of(name, {"sceIoOpen", "sceIoOpenAsync"})) {
    const auto* path = guest_string(state, state.gpr[4]);
    if (path == nullptr)
      return;
    std::uint64_t raw_disc_base = 0;
    const std::string_view psp_path(path);
    const auto lbn_marker = psp_path.find("/sce_lbn0x");
    const auto raw_disc =
        (psp_path.starts_with("disc0:") || psp_path.starts_with("umd0:")) &&
        lbn_marker != std::string_view::npos;
    if (raw_disc) {
      const auto begin = lbn_marker + std::string_view("/sce_lbn0x").size();
      const auto end = psp_path.find("_size0x", begin);
      if (end == std::string_view::npos) {
        state.gpr[2] = io_error;
        return;
      }
      const std::string lbn(psp_path.substr(begin, end - begin));
      char* parsed_end = nullptr;
      errno = 0;
      const auto lbn_value = std::strtoull(lbn.c_str(), &parsed_end, 16);
      if (errno != 0 || parsed_end == lbn.c_str() || *parsed_end != '\0') {
        state.gpr[2] = io_error;
        return;
      }
      raw_disc_base = lbn_value * 2048ULL;
    }
    const auto resolved = raw_disc ? implementation_->disc_image
                                   : implementation_->resolve_path(path);
    if ((state.gpr[5] & 0x0200U) != 0) {
      std::error_code ignored;
      std::filesystem::create_directories(resolved.parent_path(), ignored);
    }
    auto descriptor =
        ::open(resolved.c_str(), host_open_flags(state.gpr[5]),
               static_cast<mode_t>(state.gpr[6]));
    if (descriptor >= 0 && raw_disc &&
        ::lseek(descriptor, static_cast<off_t>(raw_disc_base), SEEK_SET) < 0) {
      ::close(descriptor);
      descriptor = -1;
    }
    if (descriptor < 0) {
      state.gpr[2] = io_error;
    } else {
      const auto psp_descriptor = implementation_->next_file++;
      implementation_->files.emplace(psp_descriptor, descriptor);
      if (raw_disc)
        implementation_->file_bases.emplace(psp_descriptor, raw_disc_base);
      if (name == "sceIoOpenAsync")
        implementation_->async_results.emplace(psp_descriptor,
                                                psp_descriptor);
      state.gpr[2] = static_cast<std::uint32_t>(psp_descriptor);
    }
    return;
  }
  if (name == "sceIoChdir") {
    const auto* path = guest_string(state, state.gpr[4]);
    if (path == nullptr)
      return;
    const auto resolved = implementation_->resolve_path(path);
    if (!std::filesystem::is_directory(resolved)) {
      state.gpr[2] = io_error;
    } else {
      implementation_->current_directory = resolved;
      state.gpr[2] = 0;
    }
    return;
  }
  if (name == "sceIoDopen") {
    const auto* path = guest_string(state, state.gpr[4]);
    if (path == nullptr)
      return;
    const auto resolved = implementation_->resolve_path(path);
    std::error_code error;
    Implementation::Directory directory;
    for (std::filesystem::directory_iterator iterator(resolved, error), end;
         !error && iterator != end; iterator.increment(error)) {
      directory.entries.push_back(*iterator);
    }
    if (error) {
      state.gpr[2] = io_error;
      return;
    }
    std::sort(directory.entries.begin(), directory.entries.end(),
              [](const auto& left, const auto& right) {
                return left.path().filename() < right.path().filename();
              });
    std::lock_guard lock(implementation_->objects_mutex);
    const auto uid = implementation_->next_file++;
    implementation_->directories.emplace(uid, std::move(directory));
    state.gpr[2] = static_cast<std::uint32_t>(uid);
    return;
  }
  if (name == "sceIoDread") {
    std::lock_guard lock(implementation_->objects_mutex);
    const auto found =
        implementation_->directories.find(static_cast<int>(state.gpr[4]));
    if (found == implementation_->directories.end())
      return;
    if (found->second.next >= found->second.entries.size()) {
      state.gpr[2] = 0;
      return;
    }
    constexpr std::size_t dirent_size = 352;
    auto* output = psprecomp::mapped_address(state, state.gpr[5], dirent_size);
    if (output == nullptr)
      return;
    std::memset(output, 0, dirent_size);
    const auto& entry = found->second.entries[found->second.next++];
    const auto filename = entry.path().filename().string();
    const auto mode = entry.is_directory() ? 0x11ffU : 0x21ffU;
    std::memcpy(output, &mode, sizeof(mode));
    const auto size = entry.is_regular_file() ? entry.file_size() : 0U;
    std::memcpy(output + 8U, &size, sizeof(size));
    std::error_code relative_error;
    const auto relative = std::filesystem::relative(
        entry.path(), implementation_->configuration.disc_root,
        relative_error);
    if (!relative_error) {
      const auto sector = implementation_->disc_sectors.find(
          relative.generic_string());
      if (sector != implementation_->disc_sectors.end())
        std::memcpy(output + 64U, &sector->second, sizeof(sector->second));
    }
    std::memcpy(output + 88U, filename.c_str(),
                std::min<std::size_t>(filename.size(), 255U));
    state.gpr[2] = 1;
    return;
  }
  if (name == "sceIoDclose") {
    std::lock_guard lock(implementation_->objects_mutex);
    state.gpr[2] =
        implementation_->directories.erase(static_cast<int>(state.gpr[4]))
            ? 0U
            : io_error;
    return;
  }
  if (name == "sceIoMkdir") {
    const auto* path = guest_string(state, state.gpr[4]);
    if (path == nullptr)
      return;
    std::error_code error;
    const auto created = std::filesystem::create_directories(
        implementation_->resolve_path(path), error);
    state.gpr[2] =
        !error && (created || std::filesystem::is_directory(
                                  implementation_->resolve_path(path)))
            ? 0U
            : io_error;
    return;
  }
  if (name == "sceIoGetstat") {
    const auto* path = guest_string(state, state.gpr[4]);
    auto* output = psprecomp::mapped_address(state, state.gpr[5], 88U);
    if (path == nullptr || output == nullptr)
      return;
    const auto resolved = implementation_->resolve_path(path);
    std::error_code error;
    const auto status = std::filesystem::status(resolved, error);
    if (error || !std::filesystem::exists(status)) {
      state.gpr[2] = io_error;
      return;
    }
    std::memset(output, 0, 88U);
    const auto mode = std::filesystem::is_directory(status) ? 0x11ffU : 0x21ffU;
    std::memcpy(output, &mode, sizeof(mode));
    const auto size = std::filesystem::is_regular_file(status)
                          ? std::filesystem::file_size(resolved, error)
                          : 0U;
    std::memcpy(output + 8U, &size, sizeof(size));
    std::error_code relative_error;
    const auto relative = std::filesystem::relative(
        resolved, implementation_->configuration.disc_root, relative_error);
    if (!relative_error) {
      const auto sector = implementation_->disc_sectors.find(
          relative.generic_string());
      if (sector != implementation_->disc_sectors.end())
        std::memcpy(output + 64U, &sector->second, sizeof(sector->second));
    }
    state.gpr[2] = error ? io_error : 0U;
    return;
  }
  if (name == "sceIoClose") {
    const auto found =
        implementation_->files.find(static_cast<int>(state.gpr[4]));
    if (found == implementation_->files.end())
      return;
    state.gpr[2] = ::close(found->second) == 0 ? 0U : io_error;
    implementation_->files.erase(found);
    implementation_->file_bases.erase(static_cast<int>(state.gpr[4]));
    return;
  }
  if (name == "sceIoCloseAsync") {
    const auto psp_descriptor = static_cast<int>(state.gpr[4]);
    const auto found = implementation_->files.find(psp_descriptor);
    if (found == implementation_->files.end())
      return;
    const auto result = ::close(found->second) == 0 ? 0U : io_error;
    implementation_->files.erase(found);
    implementation_->file_bases.erase(psp_descriptor);
    implementation_->async_results[psp_descriptor] =
        static_cast<std::int64_t>(static_cast<std::int32_t>(result));
    state.gpr[2] = 0U;
    return;
  }
  if (is_one_of(name, {"sceIoRead", "sceIoReadAsync", "sceIoWrite",
                       "sceIoWriteAsync"})) {
    const auto descriptor =
        implementation_->descriptor(static_cast<int>(state.gpr[4]));
    const auto size = static_cast<std::size_t>(state.gpr[6]);
    if (descriptor < 0 || !psprecomp::address_ok(state, state.gpr[5], size))
      return;
    auto* buffer = psprecomp::mapped_address(state, state.gpr[5], size);
    const auto writing = name == "sceIoWrite" || name == "sceIoWriteAsync";
    const auto result = writing ? ::write(descriptor, buffer, size)
                                : ::read(descriptor, buffer, size);
    if (name.ends_with("Async")) {
      implementation_->async_results[static_cast<int>(state.gpr[4])] =
          result < 0
              ? static_cast<std::int64_t>(static_cast<std::int32_t>(io_error))
              : result;
      state.gpr[2] = 0U;
    } else {
      state.gpr[2] = result < 0 ? io_error : static_cast<std::uint32_t>(result);
    }
    return;
  }
  if (is_one_of(name, {"sceIoLseek", "sceIoLseekAsync"})) {
    const auto descriptor =
        implementation_->descriptor(static_cast<int>(state.gpr[4]));
    if (descriptor < 0)
      return;
    auto bits = static_cast<std::uint64_t>(state.gpr[5]) |
                static_cast<std::uint64_t>(state.gpr[6]) << 32U;
    const auto base = implementation_->file_bases.find(
        static_cast<int>(state.gpr[4]));
    if (base != implementation_->file_bases.end() && state.gpr[7] == SEEK_SET)
      bits += base->second;
    auto result = ::lseek(descriptor, static_cast<std::int64_t>(bits),
                          static_cast<int>(state.gpr[7]));
    if (result >= 0 && base != implementation_->file_bases.end())
      result -= static_cast<off_t>(base->second);
    if (name == "sceIoLseekAsync") {
      implementation_->async_results[static_cast<int>(state.gpr[4])] =
          result < 0
              ? static_cast<std::int64_t>(static_cast<std::int32_t>(io_error))
              : result;
      state.gpr[2] = 0U;
      state.gpr[3] = 0U;
    } else if (result < 0) {
      state.gpr[2] = io_error;
      state.gpr[3] = 0xffffffffU;
    } else {
      state.gpr[2] = static_cast<std::uint32_t>(result);
      state.gpr[3] =
          static_cast<std::uint32_t>(static_cast<std::uint64_t>(result) >> 32U);
    }
    return;
  }
  if (is_one_of(name, {"sceIoPollAsync", "sceIoWaitAsync", "sceIoWaitAsyncCB",
                       "sceIoGetAsyncStat"})) {
    const auto descriptor = static_cast<int>(state.gpr[4]);
    const auto found = implementation_->async_results.find(descriptor);
    if (found == implementation_->async_results.end()) {
      state.gpr[2] = name == "sceIoPollAsync" ? 1U : io_error;
      return;
    }
    if (auto* output = psprecomp::mapped_address(state, state.gpr[5],
                                                 sizeof(std::int64_t)))
      std::memcpy(output, &found->second, sizeof(found->second));
    implementation_->async_results.erase(found);
    state.gpr[2] = 0U;
    return;
  }

  if (implementation_->verbose &&
      implementation_->warned.emplace(name).second) {
    std::fprintf(stderr, "[psprism:macos] unimplemented: %.*s\n",
                 static_cast<int>(name.size()), name.data());
  }
}

} // namespace psprism
