//
// Text writer for Gemini `.tbc` bytecode (Milestone 5).
//

#ifndef APOLLO_CODEGEN_TBC_WRITER_HPP
#define APOLLO_CODEGEN_TBC_WRITER_HPP

#pragma once

#include <cstdint>
#include <iosfwd>
#include <sstream>
#include <string>
#include <string_view>

namespace apollo::codegen {

/// Accumulate Gemini-compatible `.tbc` text (labels, opcodes, comments, PUSH_STR escapes).
///
/// Formatting matches handwritten Gemini programs: labels at column 0 (`name:`),
/// instructions indented with four spaces. Does not walk IR — Stage 2+ emits via this API.
class TbcWriter {
public:
    void comment(std::string_view text);
    void label(std::string_view name);
    void op(std::string_view opcode);
    void op(std::string_view opcode, std::string_view operand);
    void pushInt(std::int64_t value);
    void pushFlt(double value);
    void pushStr(std::string_view value);

    [[nodiscard]] std::string str() const;
    void write(std::ostream &out) const;

private:
    std::ostringstream buffer_;
};

/// Escape a string for a Gemini `PUSH_STR "..."` operand (`\\`, `\"`, `\n`, `\r`, `\t`).
[[nodiscard]] std::string escapeTbcString(std::string_view value);

} // namespace apollo::codegen

#endif // APOLLO_CODEGEN_TBC_WRITER_HPP
