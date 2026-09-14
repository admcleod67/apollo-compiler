#include "apollo/codegen/TbcWriter.hpp"

#include <iostream>
#include <string>

namespace {

int fail(const char *message) {
    std::cerr << "apollo_codegen_test: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    // Hand-built hello.tbc shape (matches gemini-system/programs/hello.tbc).
    {
        apollo::codegen::TbcWriter writer;
        writer.label("start");
        writer.pushStr("Hello, world");
        writer.op("PRINT_STR");
        writer.op("PRINT_EOL");
        writer.op("HALT");

        const std::string expected = "start:\n"
                                     "    PUSH_STR \"Hello, world\"\n"
                                     "    PRINT_STR\n"
                                     "    PRINT_EOL\n"
                                     "    HALT\n";
        if (writer.str() != expected) {
            return fail("hello.tbc-shaped emission mismatch");
        }
    }

    // PUSH_STR escaping for Gemini (\\, \", \\n, \\r, \\t).
    {
        apollo::codegen::TbcWriter writer;
        writer.pushStr("a\"b\\c\n\r\t");
        const std::string expected = "    PUSH_STR \"a\\\"b\\\\c\\n\\r\\t\"\n";
        if (writer.str() != expected) {
            return fail("PUSH_STR escape emission mismatch");
        }
        if (apollo::codegen::escapeTbcString("x\"y") != "x\\\"y") {
            return fail("escapeTbcString quote helper mismatch");
        }
    }

    // comment + PUSH_INT + operand form.
    {
        apollo::codegen::TbcWriter writer;
        writer.comment("demo");
        writer.label("entry");
        writer.pushInt(42);
        writer.op("STORE_VAR", "X");
        writer.op("HALT");

        const std::string text = writer.str();
        if (text.find("# demo\n") == std::string::npos ||
            text.find("entry:\n") == std::string::npos ||
            text.find("    PUSH_INT 42\n") == std::string::npos ||
            text.find("    STORE_VAR X\n") == std::string::npos) {
            return fail("comment/pushInt/op(operand) emission mismatch");
        }
    }

    {
        apollo::codegen::TbcWriter writer;
        writer.pushFlt(3.5);
        if (writer.str().find("PUSH_FLT 3.5") == std::string::npos) {
            return fail("PUSH_FLT emission mismatch");
        }
    }

    return 0;
}
