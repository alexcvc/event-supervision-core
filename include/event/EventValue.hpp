#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace app::event {

/**
 * @brief Type-erased payload used to report and carry event values.
 *
 * @details Fixed set of supported payload types — extend this variant (and
 * add a corresponding `EventDescriptor<TValue>` instantiation) as needed to
 * support new event value kinds. Passed to `EventSupervisor::trigger()` and
 * unwrapped internally via `std::get<TValue>(value)`.
 *
 * @warning `EventDescriptor<TValue>::trigger()` calls `std::get<TValue>()`
 * on the held alternative. Passing an `EventValue` holding a type that
 * does not match the target descriptor's `TValue` is a hard crash
 * (`std::bad_variant_access` / `std::terminate` via the `noexcept`
 * boundary) — callers must ensure the alternative matches the declared
 * `TValue` for the given `EventId`.
 */
using EventValue = std::variant<bool, std::int32_t, double, std::string>;

}  // namespace app::event
