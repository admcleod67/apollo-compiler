//
// Apollo Compiler version information.
//

#ifndef APOLLO_COMMON_VERSION_HPP
#define APOLLO_COMMON_VERSION_HPP

#pragma once

// Overridden from CMake via APOLLO_VERSION (PROJECT_VERSION, usually with a -dev suffix).
#ifndef APOLLO_VERSION
#define APOLLO_VERSION "0.0.0-dev"
#endif

namespace apollo::common {

/// Human-readable version string for the toolchain (SemVer-ish).
[[nodiscard]] const char *versionString() noexcept;

} // namespace apollo::common

#endif // APOLLO_COMMON_VERSION_HPP
