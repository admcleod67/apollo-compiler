#include "apollo/codegen/TbcWriter.hpp"

#include <ostream>

namespace apollo::codegen {
namespace {

constexpr char kInstrIndent[] = "    ";

} // namespace

std::string escapeTbcString(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

void TbcWriter::comment(std::string_view text) {
    buffer_ << "# " << text << '\n';
}

void TbcWriter::label(std::string_view name) {
    buffer_ << name << ":\n";
}

void TbcWriter::op(std::string_view opcode) {
    buffer_ << kInstrIndent << opcode << '\n';
}

void TbcWriter::op(std::string_view opcode, std::string_view operand) {
    buffer_ << kInstrIndent << opcode << ' ' << operand << '\n';
}

void TbcWriter::pushInt(std::int64_t value) {
    buffer_ << kInstrIndent << "PUSH_INT " << value << '\n';
}

void TbcWriter::pushFlt(double value) {
    buffer_ << kInstrIndent << "PUSH_FLT " << value << '\n';
}

void TbcWriter::pushStr(std::string_view value) {
    buffer_ << kInstrIndent << "PUSH_STR \"" << escapeTbcString(value) << "\"\n";
}

std::string TbcWriter::str() const {
    return buffer_.str();
}

void TbcWriter::write(std::ostream &out) const {
    out << buffer_.str();
}

} // namespace apollo::codegen
