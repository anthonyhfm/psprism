#include "../emitter.hpp"
#include "internal.hpp"

#include <psprecomp/allegrex_decoder.hpp>
#include <psprecomp/vfpu.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace psprecomp
{
	using namespace detail;

	bool is_psp_sdk_stub(std::string_view symbol)
	{
		static const std::unordered_set<std::string_view> stubs = {
#define PSPSDK_STUB(name) #name,
#include "../../refract/include/refract/psp_sdk_stubs.inc"
#undef PSPSDK_STUB
		};
		return stubs.contains(symbol);
	}

	void emit_project(const ElfImage &image, const std::filesystem::path &directory,
					  const CodeMap *code_map,
					  const GeneratedProjectOptions &options)
	{
#include "project_output/setup.inc"
#include "project_output/instruction_map.inc"
#include "project_output/functions.inc"
#include "project_output/psp_platform.inc"
#include "project_output/dispatch.inc"
#include "project_output/images_and_psp_main.inc"
#include "project_output/macos.inc"
#include "project_output/overlays.inc"
#include "project_output/manifests.inc"

} // namespace psprecomp
