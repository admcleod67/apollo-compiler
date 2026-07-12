#include "apollo/common/Listing.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/common/Version.hpp"

#include <iostream>
#include <string_view>

namespace {

void printUsage(std::ostream &out) {
    out << "Usage: apolloc [--version] [--help] [--list <file>]\n"
        << "\n"
        << "Apollo Compiler — multi-language toolchain for the Gemini VM.\n"
        << "\n"
        << "Options:\n"
        << "  -h, --help       Show this help\n"
        << "  -v, --version    Show version\n"
        << "  -l, --list FILE  Print a numbered source listing\n";
}

int listFile(std::string_view path) {
    const auto loaded = apollo::common::loadSourceFile(path);
    if (!loaded.file) {
        std::cerr << "apolloc: " << loaded.error << '\n';
        return 1;
    }
    apollo::common::writeListing(std::cout, *loaded.file);
    return 0;
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
    if (arg == "--list" || arg == "-l") {
        if (argc < 3) {
            std::cerr << "apolloc: --list requires a file path\n";
            printUsage(std::cerr);
            return 1;
        }
        return listFile(argv[2]);
    }

    std::cerr << "apolloc: unknown option: " << arg << '\n';
    printUsage(std::cerr);
    return 1;
}
