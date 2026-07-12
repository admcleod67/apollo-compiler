#include "apollo/common/DiagnosticEngine.hpp"
#include "apollo/common/Listing.hpp"
#include "apollo/common/SourceFile.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int fail(const char *message) {
    std::cerr << "location_diagnostics_test: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    {
        const auto empty = apollo::common::SourceFile::fromString("empty", "");
        const auto loc = empty.locationAt(0);
        if (loc != apollo::common::SourceLocation{1, 1, 0}) {
            return fail("empty file locationAt(0) should be 1:1 offset 0");
        }
        if (empty.locationAt(99).offset != 0) {
            return fail("empty file should clamp offset to 0");
        }
    }

    const std::string lfText = "ab\ncd\n";
    const std::string crlfText = "ab\r\ncd\r\n";

    const auto lf = apollo::common::SourceFile::fromString("lf.pas", lfText);
    const auto crlf = apollo::common::SourceFile::fromString("crlf.pas", crlfText);

    if (lf.locationAt(0) != apollo::common::SourceLocation{1, 1, 0}) {
        return fail("LF locationAt(0) should be 1:1");
    }
    if (lf.locationAt(1) != apollo::common::SourceLocation{1, 2, 1}) {
        return fail("LF mid-line location wrong");
    }

    // First character of line 2: 'c'
    const auto lfLine2 = lf.locationAt(lf.lineStartOffset(2));
    const auto crlfLine2 = crlf.locationAt(crlf.lineStartOffset(2));
    if (lfLine2.line != 2 || lfLine2.column != 1 || crlfLine2.line != 2 || crlfLine2.column != 1) {
        return fail("LF/CRLF line 2 starts should both be 2:1");
    }
    if (lf.lineText(2)[0] != 'c' || crlf.lineText(2)[0] != 'c') {
        return fail("line 2 content should start with c");
    }

    // EOF
    const auto lfEof = lf.locationAt(lf.text().size());
    if (lfEof.offset != lf.text().size() || lfEof.line != 2) {
        return fail("LF EOF location should be on last line");
    }

    // Listing parity: each listed line L matches locationAt(lineStartOffset(L)).line
    for (std::size_t line = 1; line <= lf.lineCount(); ++line) {
        const auto start = lf.lineStartOffset(line);
        if (start == std::string::npos) {
            return fail("lineStartOffset out of range");
        }
        if (lf.locationAt(start).line != line) {
            return fail("listing/location line parity failed");
        }
    }

    {
        std::ostringstream listed;
        apollo::common::writeListing(listed, lf);
        if (listed.str().find("   1:") == std::string::npos ||
            listed.str().find("   2:") == std::string::npos) {
            return fail("listing should still number lines 1 and 2");
        }
    }

    // Path-loaded vs fromString
    {
        const char *tmpPath = "apollo_location_tmp.pas";
        {
            std::ofstream out(tmpPath, std::ios::binary);
            if (!out) {
                return fail("cannot create temp fixture");
            }
            out << lfText;
        }
        const auto loaded = apollo::common::loadSourceFile(tmpPath);
        std::remove(tmpPath);
        if (!loaded.file) {
            return fail("failed to load temp fixture");
        }
        if (loaded.file->locationAt(0) != lf.locationAt(0) ||
            loaded.file->locationAt(lf.lineStartOffset(2)).line !=
                lf.locationAt(lf.lineStartOffset(2)).line) {
            return fail("path-loaded locations should match fromString");
        }
    }

    // Diagnostics
    {
        apollo::common::DiagnosticEngine engine(lf);
        engine.report(apollo::common::DiagnosticSeverity::Error, lf.locationAt(0), "boom");
        engine.report(apollo::common::DiagnosticSeverity::Warning, lf.locationAt(1), "careful");
        if (engine.errorCount() != 1) {
            return fail("errorCount should count errors only");
        }
        if (engine.diagnostics().size() != 2) {
            return fail("expected two diagnostics");
        }

        std::ostringstream out;
        engine.write(out);
        const std::string expected =
            "lf.pas:1:1: error: boom\n"
            "lf.pas:1:2: warning: careful\n";
        if (out.str() != expected) {
            std::cerr << "got:\n" << out.str();
            return fail("diagnostic write format mismatch");
        }
    }

    std::cout << "location_diagnostics_test: ok\n";
    return 0;
}
