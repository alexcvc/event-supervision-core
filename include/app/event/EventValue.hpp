#pragma once

#include <cstdint>
#include <variant>

namespace app::event
{

// Fixed set of supported payload types — extend as needed.
using EventValue = std::variant<bool, std::int32_t>;

} // namespace app::event
