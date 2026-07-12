#include "apollo/common/Listing.hpp"
#include "apollo/common/SourceFile.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {

int fail(const char *message) {
    std::cerr << "source_listing_test: " << message << '\n';
    return 1;
}

std::string listingOf(const apollo::common::SourceFile &source) {
    std::ostringstream out;
    apollo::common::writeListing(out, source);
    return out.str();
}

} // namespace

int main() {
    {
        const auto empty = apollo::common::SourceFile::fromString("empty", "");
        if (empty.lineCount() != 0) {
            return fail("empty text should have zero lines");
        }
        if (!listingOf(empty).empty()) {
            return fail("empty listing should write nothing");
        }
    }

    {
        const auto onlyNl = apollo::common::SourceFile::fromString("nl", "\n");
        if (onlyNl.lineCount() != 1 || onlyNl.lineText(1) != "") {
            return fail("lone newline should be one empty line");
        }
    }

    const std::string lfText = "program Hello;\nbegin\nend.\n";
    const std::string crlfText = "program Hello;\r\nbegin\r\nend.\r\n";
    const std::string expected =
        "   1: program Hello;\n"
        "   2: begin\n"
        "   3: end.\n";

    const auto lf = apollo::common::SourceFile::fromString("lf.pas", lfText);
    const auto crlf = apollo::common::SourceFile::fromString("crlf.pas", crlfText);

    if (lf.lineCount() != 3 || crlf.lineCount() != 3) {
        return fail("LF and CRLF should both yield three lines");
    }
    if (lf.lineText(1) != "program Hello;" || crlf.lineText(1) != "program Hello;") {
        return fail("line 1 text mismatch");
    }
    if (listingOf(lf) != expected || listingOf(crlf) != expected) {
        return fail("LF and CRLF listings should match golden output");
    }

    {
        const auto noTrailing = apollo::common::SourceFile::fromString("x", "a\nb");
        if (noTrailing.lineCount() != 2) {
            return fail("unterminated last line should still count");
        }
        if (noTrailing.lineText(2) != "b") {
            return fail("unterminated last line text wrong");
        }
    }

    {
        const auto missing = apollo::common::loadSourceFile(
            "tests/fixtures/does-not-exist-apollo-listing.pas");
        if (missing.file) {
            return fail("missing path should not produce a SourceFile");
        }
        if (missing.error.empty()) {
            return fail("missing path should set an error message");
        }
    }

    std::cout << "source_listing_test: ok\n";
    return 0;
}
