#include "apollo/common/Version.hpp"

#include <iostream>
#include <string_view>

namespace {

void printUsage(std::ostream &out) {
    out << "Usage: apolloc [--version] [--help]\n"
        << "\n"
        << "Apollo Compiler — multi-language toolchain for the Gemini VM.\n"
        << "Front-ends and codegen are not wired yet (Milestone 0 skeleton).\n";
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        printUsage(std::cout);
        return 0;
    }

    const std::string_view arg = argv[1];
    if (arg == "--version" || arg == "-v") {
        std::cout << "apolloc " << apollo::common::versionString() << '\n';
        return 0;
    }
    if (arg == "--help" || arg == "-h") {
        printUsage(std::cout);
        return 0;
    }

    std::cerr << "apolloc: unknown option: " << arg << '\n';
    printUsage(std::cerr);
    return 1;
}
