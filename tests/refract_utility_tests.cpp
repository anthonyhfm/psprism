#include "utility_data.hpp"

#include <cstdint>
#include <string>
#include <vector>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) return __LINE__;                                         \
  } while (false)

int main() {
  const auto sfo = refract::utility::make_savedata_sfo(
      "Example Game", "Slot 1", "Progress at the first checkpoint");
  CHECK(refract::utility::sfo_string(sfo, "TITLE") == "Example Game");
  CHECK(refract::utility::sfo_string(sfo, "SAVEDATA_TITLE") == "Slot 1");
  CHECK(refract::utility::sfo_string(sfo, "SAVEDATA_DETAIL") ==
        "Progress at the first checkpoint");
  CHECK(refract::utility::sfo_string(sfo, "MISSING").empty());

  const std::string oversized(2048U, 'x');
  const auto bounded =
      refract::utility::make_savedata_sfo(oversized, oversized, oversized);
  CHECK(refract::utility::sfo_string(bounded, "TITLE").size() == 127U);
  CHECK(refract::utility::sfo_string(bounded, "SAVEDATA_DETAIL").size() ==
        1023U);

  CHECK(refract::utility::sfo_string({}, "TITLE").empty());
  auto corrupt = sfo;
  corrupt.resize(24U);
  CHECK(refract::utility::sfo_string(corrupt, "TITLE").empty());
}
