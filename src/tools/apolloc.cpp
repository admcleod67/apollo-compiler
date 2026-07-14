#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/Listing.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/common/Version.hpp"
#include "apollo/pascal/AstDump.hpp"
#include "apollo/pascal/Parser.hpp"
#include "apollo/pascal/Scanner.hpp"
#include "apollo/pascal/SymbolTable.hpp"
#include "apollo/pascal/TokenDump.hpp"

#include <iostream>
#include <string_view>

namespace {

void printUsage(std::ostream &out) {
    out << "Usage: apolloc [--version] [--help] [--list <file>] [--tokens <file>] [--ast <file>]\n"
        << "\n"
        << "Apollo Compiler — multi-language toolchain for the Gemini VM.\n"
        << "\n"
        << "Options:\n"
        << "  -h, --help         Show this help\n"
        << "  -v, --version      Show version\n"
        << "  -l, --list FILE    Print a numbered source listing\n"
        << "  -t, --tokens FILE  Scan Pascal source and dump the token stream\n"
        << "  -a, --ast FILE     Parse Pascal source and dump the AST\n";
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

int tokensFile(std::string_view path) {
    const auto loaded = apollo::common::loadSourceFile(path);
    if (!loaded.file) {
        std::cerr << "apolloc: " << loaded.error << '\n';
        return 1;
    }

    apollo::common::DiagnosticEngine diagnostics(*loaded.file);
    const auto stream = apollo::pascal::scan(*loaded.file, diagnostics);
    diagnostics.write(std::cerr);
    apollo::pascal::writeTokenDump(std::cout, stream);
    return diagnostics.errorCount() == 0 ? 0 : 1;
}

int astFile(std::string_view path) {
    const auto loaded = apollo::common::loadSourceFile(path);
    if (!loaded.file) {
        std::cerr << "apolloc: " << loaded.error << '\n';
        return 1;
    }

    apollo::common::DiagnosticEngine diagnostics(*loaded.file);
    const auto stream = apollo::pascal::scan(*loaded.file, diagnostics);
    const auto program = apollo::pascal::parse(*loaded.file, stream, diagnostics);
    if (program) {
        (void)apollo::pascal::buildSymbolTable(*program, diagnostics);
        apollo::pascal::writeAstDump(std::cout, *program);
    }
    diagnostics.write(std::cerr);
    return diagnostics.errorCount() == 0 ? 0 : 1;
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
    if (arg == "--tokens" || arg == "-t") {
        if (argc < 3) {
            std::cerr << "apolloc: --tokens requires a file path\n";
            printUsage(std::cerr);
            return 1;
        }
        return tokensFile(argv[2]);
    }
    if (arg == "--ast" || arg == "-a") {
        if (argc < 3) {
            std::cerr << "apolloc: --ast requires a file path\n";
            printUsage(std::cerr);
            return 1;
        }
        return astFile(argv[2]);
    }

    std::cerr << "apolloc: unknown option: " << arg << '\n';
    printUsage(std::cerr);
    return 1;
}
