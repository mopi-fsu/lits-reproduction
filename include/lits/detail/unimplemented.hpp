#pragma once

#include <stdexcept>
#include <string>

namespace lits::detail {

[[noreturn]] inline void unimplemented(const char* operation) {
    throw std::logic_error(std::string(operation) + " is not implemented yet");
}

} // namespace lits::detail
