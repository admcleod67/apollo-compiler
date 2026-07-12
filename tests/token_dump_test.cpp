#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/SourceFile.hpp"
#include "apollo/pascal/Scanner.hpp"
#include "apollo/pascal/TokenDump.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {

int fail(const char *message) {
    std::cerr << "token_dump_test: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    const auto source = apollo::common::SourceFile::fromString("tiny.pas", "program X;");
    apollo::common::DiagnosticEngine diagnostics(source);
    const auto stream = apollo::pascal::scan(source, diagnostics);
    if (diagnostics.errorCount() != 0) {
        return fail("tiny program should scan cleanly");
    }

    std::ostringstream out;
    apollo::pascal::writeTokenDump(out, stream);
    const std::string dump = out.str();

    if (dump.find("KeywordProgram") == std::string::npos) {
        return fail("dump should include KeywordProgram");
    }
    if (dump.find("Identifier") == std::string::npos || dump.find("X") == std::string::npos) {
        return fail("dump should include Identifier X");
    }
    if (dump.find("Semicolon") == std::string::npos) {
        return fail("dump should include Semicolon");
    }
    if (dump.find("EndOfFile") == std::string::npos) {
        return fail("dump should end with EndOfFile kind");
    }

    // First line should use the range/kind/lexeme field shape.
    if (dump.rfind("1:1-", 0) != 0) {
        return fail("first dump line should start with 1:1-");
    }

    std::cout << "token_dump_test: ok\n";
    return 0;
}
