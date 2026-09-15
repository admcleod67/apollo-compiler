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

    // Operands must read back as the same double, not the default 6 significant digits.
    {
        const double values[] = {3.14159265358979, 123456789.5, 1e-7, -0.1, 1e20};
        for (const double value : values) {
            apollo::codegen::TbcWriter writer;
            writer.pushFlt(value);
            const std::string text = writer.str();
            const std::size_t start = text.find("PUSH_FLT ") + 9;
            const std::string operand = text.substr(start, text.find('\n', start) - start);
            if (std::stod(operand) != value) {
                return fail("PUSH_FLT operand does not round-trip");
            }
        }
    }

    // Integral values still look like floats.
    {
        apollo::codegen::TbcWriter writer;
        writer.pushFlt(1.0);
        writer.pushFlt(0.0);
        if (writer.str() != "    PUSH_FLT 1.0\n    PUSH_FLT 0.0\n") {
            return fail("integral PUSH_FLT operands should keep a decimal point");
        }
    }

    return 0;
}
