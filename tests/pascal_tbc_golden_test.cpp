#include "apollo/codegen/Emit.hpp"
#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/pascal/Analyse.hpp"
#include "apollo/pascal/Parser.hpp"
#include "apollo/pascal/Scanner.hpp"
#include "apollo/pascal/ir/Lower.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr const char *kStems[] = {"hello", "count", "arith", "arrays", "reals", "subprograms"};

int fail(const std::string &message) {
    std::cerr << "pascal_tbc_golden_test: " << message << '\n';
    return 1;
}

bool updateGoldens() {
    return std::getenv("APOLLO_UPDATE_GOLDEN") != nullptr;
}

std::string readFile(const fs::path &path, std::string &error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path.string();
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        error = "cannot read " + path.string();
        return {};
    }
    return buffer.str();
}

bool writeFile(const fs::path &path, const std::string &text, std::string &error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot write " + path.string();
        return false;
    }
    out << text;
    if (!out) {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

std::vector<std::string> splitLines(const std::string &text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
        if (start == text.size()) {
            break;
        }
    }
    return lines;
}

void reportFirstDifference(const std::string &stem, const std::string &expected,
                           const std::string &actual) {
    const auto expectedLines = splitLines(expected);
    const auto actualLines = splitLines(actual);
    const std::size_t n = std::min(expectedLines.size(), actualLines.size());
    std::size_t line = 0;
    while (line < n && expectedLines[line] == actualLines[line]) {
        ++line;
    }
    std::cerr << "pascal_tbc_golden_test: " << stem << ".tbc mismatch at line " << (line + 1)
              << '\n';
    if (line < expectedLines.size()) {
        std::cerr << "  expected: " << expectedLines[line] << '\n';
    } else {
        std::cerr << "  expected: <end of file>\n";
    }
    if (line < actualLines.size()) {
        std::cerr << "  actual:   " << actualLines[line] << '\n';
    } else {
        std::cerr << "  actual:   <end of file>\n";
    }
    if (line > 0) {
        std::cerr << "  previous: " << expectedLines[line - 1] << '\n';
    }
    std::cerr << "  regenerate with APOLLO_UPDATE_GOLDEN=1\n";
}

std::string emitPas(const fs::path &pasPath, std::string &error) {
    const auto loaded = apollo::common::loadSourceFile(pasPath.string());
    if (!loaded.file) {
        error = loaded.error;
        return {};
    }
    const apollo::common::SourceFile &source = *loaded.file;
    apollo::common::DiagnosticEngine diagnostics(source);
    const apollo::pascal::TokenStream tokens = apollo::pascal::scan(source, diagnostics);
    const auto program = apollo::pascal::parse(source, tokens, diagnostics);
    if (!program) {
        std::ostringstream out;
        diagnostics.write(out);
        error = "parse failed for " + pasPath.string() + "\n" + out.str();
        return {};
    }
    auto symbols = apollo::pascal::analyse(*program, diagnostics);
    if (diagnostics.errorCount() != 0) {
        std::ostringstream out;
        diagnostics.write(out);
        error = "analyse failed for " + pasPath.string() + "\n" + out.str();
        return {};
    }
    const apollo::ir::Module module =
        apollo::pascal::ir::lowerToIr(*program, symbols, diagnostics);
    if (diagnostics.errorCount() != 0) {
        std::ostringstream out;
        diagnostics.write(out);
        error = "lower failed for " + pasPath.string() + "\n" + out.str();
        return {};
    }
    std::string tbc = apollo::codegen::emitTbc(module, diagnostics);
    if (diagnostics.errorCount() != 0 || tbc.empty()) {
        std::ostringstream out;
        diagnostics.write(out);
        error = "emit failed for " + pasPath.string() + "\n" + out.str();
        return {};
    }
    return tbc;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        return fail("usage: pascal_tbc_golden_test <golden-dir>");
    }
    const fs::path dir(argv[1]);
    if (!fs::is_directory(dir)) {
        return fail("not a directory: " + dir.string());
    }

    const bool update = updateGoldens();
    int failures = 0;
    for (const char *stem : kStems) {
        const fs::path pasPath = dir / (std::string(stem) + ".pas");
        const fs::path tbcPath = dir / (std::string(stem) + ".tbc");
        if (!fs::is_regular_file(pasPath)) {
            std::cerr << "pascal_tbc_golden_test: missing fixture " << pasPath.string() << '\n';
            ++failures;
            continue;
        }

        std::string error;
        const std::string actual = emitPas(pasPath, error);
        if (actual.empty()) {
            std::cerr << "pascal_tbc_golden_test: " << error << '\n';
            ++failures;
            continue;
        }

        if (update) {
            if (!writeFile(tbcPath, actual, error)) {
                std::cerr << "pascal_tbc_golden_test: " << error << '\n';
                ++failures;
            }
            continue;
        }

        if (!fs::is_regular_file(tbcPath)) {
            std::cerr << "pascal_tbc_golden_test: missing golden " << tbcPath.string()
                      << " (set APOLLO_UPDATE_GOLDEN=1 to create it)\n";
            ++failures;
            continue;
        }

        const std::string expected = readFile(tbcPath, error);
        if (!error.empty()) {
            std::cerr << "pascal_tbc_golden_test: " << error << '\n';
            ++failures;
            continue;
        }
        if (expected != actual) {
            reportFirstDifference(stem, expected, actual);
            ++failures;
        }
    }

    if (failures != 0) {
        return 1;
    }
    return 0;
}
