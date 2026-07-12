//
// Apollo Compiler version information.
//

#ifndef APOLLO_COMMON_VERSION_HPP
#define APOLLO_COMMON_VERSION_HPP

#pragma once

namespace apollo::common {

/// Human-readable version string for the toolchain (SemVer-ish).
[[nodiscard]] const char *versionString() noexcept;

} // namespace apollo::common

#endif // APOLLO_COMMON_VERSION_HPP
