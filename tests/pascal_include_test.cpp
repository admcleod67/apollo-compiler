#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/pascal/Analyse.hpp"
#include "apollo/pascal/Parser.hpp"
#include "apollo/pascal/Scanner.hpp"
#include "apollo/pascal/TokenKind.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

namespace fs = std::filesystem;

int fail(const char *message) {
    std::cerr << "pascal_include_test: " << message << '\n';
    return 1;
}

std::string fixtureDir() {
    // Prefer source-tree fixtures via compile definition; fall back to relative path.
#ifdef APOLLO_SOURCE_DIR
    return std::string(APOLLO_SOURCE_DIR) + "/tests/fixtures/include";
#else
    return "tests/fixtures/include";
#endif
}

bool hasWarning(const apollo::common::DiagnosticEngine &diagnostics) {
    for (const auto &d : diagnostics.diagnostics()) {
        if (d.severity == apollo::common::DiagnosticSeverity::Warning) {
            return true;
        }
    }
    return false;
}

bool containsIdent(const apollo::pascal::TokenStream &tokens, std::string_view name) {
    for (const auto &tok : tokens) {
        if (tok.kind == apollo::pascal::TokenKind::Identifier && tok.lexeme == name) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    const fs::path fixtures = fixtureDir();

    // Happy-path include splices const name into the token stream.
    {
        const auto loaded = apollo::common::loadSourceFile((fixtures / "main.pas").string());
        if (!loaded.file) {
            return fail("cannot load main.pas");
        }
        apollo::common::DiagnosticEngine diagnostics(*loaded.file);
        const auto scanned = apollo::pascal::scan(*loaded.file, diagnostics);
        if (diagnostics.errorCount() != 0) {
            return fail("main.pas include should scan cleanly");
        }
        if (!containsIdent(scanned.tokens, "answer")) {
            return fail("included identifier 'answer' missing from token stream");
        }
        if (scanned.sources.size() < 2) {
            return fail("scan should own root and included SourceFile");
        }
        const auto program = apollo::pascal::parse(*loaded.file, scanned.tokens, diagnostics);
        if (!program) {
            return fail("main.pas should parse");
        }
        (void)apollo::pascal::analyse(*program, diagnostics);
        if (diagnostics.errorCount() != 0) {
            return fail("main.pas should analyse cleanly");
        }
    }

    // Nested includes.
    {
        const auto loaded = apollo::common::loadSourceFile((fixtures / "nested_main.pas").string());
        if (!loaded.file) {
            return fail("cannot load nested_main.pas");
        }
        apollo::common::DiagnosticEngine diagnostics(*loaded.file);
        const auto scanned = apollo::pascal::scan(*loaded.file, diagnostics);
        if (diagnostics.errorCount() != 0 || !containsIdent(scanned.tokens, "nestedVal")) {
            return fail("nested includes should splice nestedVal");
        }
    }

    // Cyclic include.
    {
        const auto loaded = apollo::common::loadSourceFile((fixtures / "cycle_a.pas").string());
        if (!loaded.file) {
            return fail("cannot load cycle_a.pas");
        }
        apollo::common::DiagnosticEngine diagnostics(*loaded.file);
        const auto scanned = apollo::pascal::scan(*loaded.file, diagnostics);
        (void)scanned;
        if (diagnostics.errorCount() == 0) {
            return fail("cyclic include should diagnose");
        }
    }

    // Missing include file.
    {
        const auto loaded = apollo::common::loadSourceFile((fixtures / "missing.pas").string());
        if (!loaded.file) {
            return fail("cannot load missing.pas");
        }
        apollo::common::DiagnosticEngine diagnostics(*loaded.file);
        const auto scanned = apollo::pascal::scan(*loaded.file, diagnostics);
        (void)scanned;
        if (diagnostics.errorCount() == 0) {
            return fail("missing include should diagnose");
        }
    }

    // Unknown directive is a warning only.
    {
        const auto loaded = apollo::common::loadSourceFile((fixtures / "unknown_dir.pas").string());
        if (!loaded.file) {
            return fail("cannot load unknown_dir.pas");
        }
        apollo::common::DiagnosticEngine diagnostics(*loaded.file);
        const auto scanned = apollo::pascal::scan(*loaded.file, diagnostics);
        if (diagnostics.errorCount() != 0) {
            return fail("unknown directive should not be an error");
        }
        if (!hasWarning(diagnostics)) {
            return fail("unknown directive should warn");
        }
        const auto program = apollo::pascal::parse(*loaded.file, scanned.tokens, diagnostics);
        if (!program) {
            return fail("unknown_dir.pas should parse");
        }
        (void)apollo::pascal::analyse(*program, diagnostics);
        if (diagnostics.errorCount() != 0) {
            return fail("unknown_dir.pas should analyse with warnings only");
        }
    }

    // Include depth limit (32).
    {
        const fs::path dir = fs::temp_directory_path() / "apollo_include_depth";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        if (ec) {
            return fail("cannot create temp include depth directory");
        }
        for (int i = 0; i < 33; ++i) {
            std::ofstream out(dir / ("d" + std::to_string(i) + ".inc"));
            if (i + 1 < 33) {
                out << "{$I d" << (i + 1) << ".inc}\n";
            } else {
                out << "const depthOk = 1;\n";
            }
        }
        std::ofstream mainOut(dir / "depth_main.pas");
        mainOut << "program Depth;\n{$I d0.inc}\nbegin end.\n";
        mainOut.close();

        const auto loaded = apollo::common::loadSourceFile((dir / "depth_main.pas").string());
        if (!loaded.file) {
            return fail("cannot load depth_main.pas");
        }
        apollo::common::DiagnosticEngine diagnostics(*loaded.file);
        const auto scanned = apollo::pascal::scan(*loaded.file, diagnostics);
        (void)scanned;
        if (diagnostics.errorCount() == 0) {
            return fail("include depth overflow should diagnose");
        }
        fs::remove_all(dir, ec);
    }

    return 0;
}
