#include "apollo/common/Version.hpp"

#include <cstring>
#include <iostream>

int main() {
    const char *version = apollo::common::versionString();
    if (version == nullptr || std::strlen(version) == 0) {
        std::cerr << "smoke_test: versionString() returned empty\n";
        return 1;
    }

    std::cout << "apollo-common " << version << '\n';
    return 0;
}
