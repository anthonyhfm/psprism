#include <refract/refract.hpp>
#include <refract/psp_sdk_stubs.hpp>

#include "host/host.hpp"
#include "stubs/io/devctl_state.hpp"
#include "stubs/io/io_state.hpp"
#include "stubs/kernel/mailbox_state.hpp"
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
#include <deque>
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

namespace refract {
namespace {

constexpr std::uint32_t unimplemented = 0x8002013aU;
constexpr std::uint32_t io_error = 0x80010005U;
constexpr std::uint32_t wait_timeout = 0x800201a8U;
constexpr std::uint32_t semaphore_zero = 0x800201adU;
constexpr std::uint32_t unknown_mailbox = 0x8002019bU;
constexpr std::uint32_t mailbox_no_message = 0x800201b2U;
constexpr std::uint32_t wait_deleted = 0x800201b5U;
constexpr std::uint32_t out_of_memory = 0x80020190U;
constexpr std::uint32_t utility_busy = 0x80110001U;
constexpr std::uint32_t utility_cancelled = 0x80110302U;
constexpr std::uint32_t return_address = 0xfffffff0U;
constexpr std::uint32_t initial_thread_stack_size = 0x40000U;

thread_local int current_thread_id = 1;
thread_local bool guest_execution_locked = false;
// The PSP cannot context-switch normal guest threads while CPU interrupts are
// suspended.  Keep the state with the host thread that owns each guest
// context so import-boundary scheduling cannot split an interrupt-protected
// critical section.
thread_local bool guest_interrupts_enabled = true;

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
    std::string name;
    int count{};
    int maximum{};
  };

  struct Mutex {
    std::mutex mutex;
    std::condition_variable changed;
    std::string name;
    int lock_count{};
    int owner_thread_id{-1};
  };

  struct EventFlag {
    std::mutex mutex;
    std::condition_variable changed;
    std::uint32_t bits{};
  };

  struct Mailbox {
    std::mutex mutex;
    std::condition_variable changed;
    std::string name;
    std::uint32_t attributes{};
    std::deque<mailbox_state::Message> messages;
    bool deleted{};
  };

  struct MemoryBlock {
    std::uint32_t address{};
    std::uint32_t size{};
  };

  struct FixedPool {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<MemoryBlock> backing;
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

  struct GeCallback {
    std::uint32_t signal_entry{};
    std::uint32_t signal_argument{};
    std::uint32_t finish_entry{};
    std::uint32_t finish_argument{};
  };

  struct Module {
    std::filesystem::path path;
    bool started{};
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
  std::vector<std::uint8_t> volatile_memory;
  Configuration configuration;
  std::unordered_set<std::string> warned;
  std::unordered_map<int, int> files;
  std::unordered_map<int, io_state::FileView> file_views;
  std::unordered_set<int> sector_files;
  std::unordered_map<int, std::int64_t> async_results;
  std::unordered_map<int, Directory> directories;
  std::filesystem::path current_directory;
  std::filesystem::path disc_image;
  std::unordered_map<std::string, std::uint32_t> disc_sectors;
  std::mutex objects_mutex;
  // Allegrex user threads share one CPU.  Generated guest code therefore
  // runs cooperatively under this lock and releases it only while an HLE call
  // blocks, matching PSP scheduling instead of racing guest memory on several
  // host cores.
  std::mutex guest_execution_mutex;
  std::condition_variable guest_execution_changed;
  std::uint64_t next_guest_ticket{};
  std::uint64_t serving_guest_ticket{};
  std::mutex exit_mutex;
  std::condition_variable exit_changed;
  std::unordered_map<int, std::shared_ptr<GuestThread>> threads;
  std::unordered_map<int, std::shared_ptr<Semaphore>> semaphores;
  std::unordered_map<int, std::shared_ptr<Mutex>> mutexes;
  std::unordered_map<int, std::shared_ptr<EventFlag>> event_flags;
  std::unordered_map<int, std::shared_ptr<Mailbox>> mailboxes;
  std::unordered_map<int, MemoryBlock> memory_blocks;
  std::unordered_map<int, std::shared_ptr<FixedPool>> fixed_pools;
  std::unordered_map<int, std::shared_ptr<VariablePool>> variable_pools;
  std::unordered_map<int, Callback> callbacks;
  std::unordered_map<int, GeCallback> ge_callbacks;
  std::unordered_map<int, Module> modules;
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

using Implementation = Runtime::Implementation;

void request_guest_exit(Implementation& implementation) {
  implementation.exit_requested = true;
  implementation.exit_changed.notify_all();
  std::lock_guard lock(implementation.objects_mutex);
  for (const auto& [uid, semaphore] : implementation.semaphores) {
    static_cast<void>(uid);
    semaphore->changed.notify_all();
  }
  for (const auto& [uid, mutex] : implementation.mutexes) {
    static_cast<void>(uid);
    mutex->changed.notify_all();
  }
  for (const auto& [uid, event] : implementation.event_flags) {
    static_cast<void>(uid);
    event->changed.notify_all();
  }
  for (const auto& [uid, mailbox] : implementation.mailboxes) {
    static_cast<void>(uid);
    mailbox->changed.notify_all();
  }
  for (const auto& [uid, pool] : implementation.fixed_pools) {
    static_cast<void>(uid);
    pool->changed.notify_all();
  }
  for (const auto& [uid, pool] : implementation.variable_pools) {
    static_cast<void>(uid);
    pool->changed.notify_all();
  }
}

void acquire_guest_execution(Implementation& implementation) {
  std::unique_lock lock(implementation.guest_execution_mutex);
  const auto ticket = implementation.next_guest_ticket++;
  implementation.guest_execution_changed.wait(
      lock, [&] { return ticket == implementation.serving_guest_ticket; });
  guest_execution_locked = true;
}

void release_guest_execution(Implementation& implementation) {
  {
    std::lock_guard lock(implementation.guest_execution_mutex);
    guest_execution_locked = false;
    ++implementation.serving_guest_ticket;
  }
  implementation.guest_execution_changed.notify_all();
}

struct GuestExecutionLock {
  explicit GuestExecutionLock(Implementation& implementation)
      : implementation_(implementation) {
    acquire_guest_execution(implementation_);
  }

  ~GuestExecutionLock() { release_guest_execution(implementation_); }

  GuestExecutionLock(const GuestExecutionLock&) = delete;
  GuestExecutionLock& operator=(const GuestExecutionLock&) = delete;

private:
  Implementation& implementation_;
};

struct GuestExecutionPause {
  explicit GuestExecutionPause(Implementation& implementation)
      : implementation_(implementation), paused_(guest_execution_locked) {
    if (paused_) {
      release_guest_execution(implementation_);
    }
  }

  ~GuestExecutionPause() {
    if (paused_) {
      acquire_guest_execution(implementation_);
    }
  }

  GuestExecutionPause(const GuestExecutionPause&) = delete;
  GuestExecutionPause& operator=(const GuestExecutionPause&) = delete;

private:
  Implementation& implementation_;
  bool paused_{};
};

void execute_guest(Implementation& implementation, psprecomp::State& state) {
  GuestExecutionLock execution_lock(implementation);
  implementation.configuration.guest_executor(state);
}

void yield_guest(Implementation& implementation) {
  if (!guest_interrupts_enabled)
    return;
  GuestExecutionPause pause(implementation);
}

static void prepare_state_for_thread(Implementation& implementation,
                                    psprecomp::State& state) {
  state.scratchpad = implementation.scratchpad.data();
  state.scratchpad_size = implementation.scratchpad.size();
  state.video_memory = implementation.video_memory.data();
  state.video_memory_size = implementation.video_memory.size();
  state.volatile_memory = implementation.volatile_memory.data();
  state.volatile_memory_size = implementation.volatile_memory.size();
}

bool dispatch_guest_callback(Implementation& implementation,
                             psprecomp::State& state,
                             std::uint32_t entry,
                             std::uint32_t first_argument,
                             std::uint32_t second_argument,
                             std::uint32_t third_argument = 0U) {
  if (entry == 0U || !implementation.configuration.guest_executor)
    return false;
  constexpr std::uint32_t callback_stack_size = 0x4000U;
  std::uint32_t stack{};
  {
    std::lock_guard lock(implementation.objects_mutex);
    stack = implementation.allocate_heap(callback_stack_size);
  }
  if (stack == 0U)
    return false;
  auto callback_state = state;
  callback_state.pc = entry;
  const auto guest_gp = callback_state.gpr[28];
  std::fill_n(callback_state.gpr, 32U, 0U);
  callback_state.gpr[4] = first_argument;
  callback_state.gpr[5] = second_argument;
  callback_state.gpr[6] = third_argument;
  callback_state.gpr[28] = guest_gp;
  callback_state.gpr[29] = stack + callback_stack_size - 64U;
  callback_state.gpr[31] = return_address;
  callback_state.branch_pending = false;
  callback_state.stop_reason = psprecomp::StopReason::running;
  callback_state.fault_address = 0U;
  callback_state.fault_instruction = 0U;
  callback_state.fault_pc = 0U;
  implementation.configuration.guest_executor(callback_state);
  {
    std::lock_guard lock(implementation.objects_mutex);
    implementation.free_heap(stack, callback_stack_size);
  }
  if (implementation.verbose &&
      callback_state.stop_reason != psprecomp::StopReason::returned) {
    std::fprintf(stderr,
                 "[psprism:callback] stop entry=%08x reason=%u pc=%08x "
                 "fault=%08x\n",
                 entry, static_cast<unsigned>(callback_state.stop_reason),
                 callback_state.pc, callback_state.fault_address);
  }
  return callback_state.stop_reason == psprecomp::StopReason::returned;
}

bool dispatch_notify_callback(Implementation& implementation,
                             psprecomp::State& state, int uid,
                             std::uint32_t argument) {
  Implementation::Callback callback;
  {
    std::lock_guard lock(implementation.objects_mutex);
    const auto found = implementation.callbacks.find(uid);
    if (found == implementation.callbacks.end() ||
        !implementation.configuration.guest_executor)
      return false;
    callback = found->second;
  }
  return dispatch_guest_callback(implementation, state, callback.entry,
                                 static_cast<std::uint32_t>(uid), argument,
                                 callback.common_argument);
}

namespace {

#include "stubs/dispatch_includes.inc"
} // namespace

namespace pspsdk {

#define PSPSDK_STUB(name)                                                  \
  void name(psprecomp::State& state) {                                     \
    auto& implementation = Runtime::instance().implementation();           \
    ::refract::name(implementation, state);                                \
    if (implementation.exit_requested) {                                   \
      state.stop_reason = psprecomp::StopReason::returned;                  \
    } else {                                                               \
      yield_guest(implementation);                                         \
    }                                                                      \
  }
#include <refract/psp_sdk_stubs.inc>
#undef PSPSDK_STUB
} // namespace pspsdk


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
  implementation_->volatile_memory.assign(4U * 1024U * 1024U, 0);
  implementation_->start_monotonic_microseconds =
      host::monotonic_microseconds();
  implementation_->start_unix_seconds = host::unix_seconds();
  implementation_->heap_cursor =
      align_up(static_cast<std::uint32_t>(configuration.image_size), 64U);
  implementation_->stack_cursor =
      size > initial_thread_stack_size
          ? static_cast<std::uint32_t>(size - initial_thread_stack_size)
          : static_cast<std::uint32_t>(size);
  constexpr std::uint32_t user_memory_start = 0x08800000U;
  const auto image_start = base + configuration.image_start;
  if (base <= user_memory_start && image_start > user_memory_start &&
      image_start <= base + size) {
    implementation_->free_heap_blocks.push_back(
        {user_memory_start, image_start - user_memory_start});
  }
  if (configuration.disc_root.empty()) {
    if (const auto* value = std::getenv("REFRACT_DISC_ROOT")) {
      configuration.disc_root = value;
    } else {
      configuration.disc_root = std::filesystem::current_path() / "disc";
    }
  }
  if (configuration.writable_root.empty()) {
    if (const auto* value = std::getenv("REFRACT_WRITABLE_ROOT")) {
      configuration.writable_root = value;
    } else {
      configuration.writable_root =
          std::filesystem::current_path() / ".refract" / "ms0";
    }
  }
  std::filesystem::create_directories(configuration.writable_root);
  implementation_->configuration = std::move(configuration);
  implementation_->current_directory = implementation_->configuration.disc_root;
  if (const auto* value = std::getenv("REFRACT_DISC_IMAGE")) {
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
  state.volatile_memory = implementation_->volatile_memory.data();
  state.volatile_memory_size = implementation_->volatile_memory.size();
}

void Runtime::execute_guest(psprecomp::State& state) {
  ::refract::execute_guest(*implementation_, state);
}

void Runtime::run_host_loop() {
  host::run_event_loop();
  request_guest_exit(*implementation_);
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
      if (thread->uid != current_thread_id && thread->host_thread.joinable())
        thread->host_thread.join();
    }
  }
}

Runtime::Implementation& Runtime::implementation() {
  return *implementation_;
}

void Runtime::warn_unimplemented(psprecomp::State& state,
                                std::string_view name) {
  state.gpr[2] = unimplemented;
  if (implementation_->verbose && implementation_->warned.emplace(name).second) {
    std::fprintf(stderr, "[psprism:macos] unimplemented: %.*s\n",
                 static_cast<int>(name.size()), name.data());
  }
}

} // namespace refract
