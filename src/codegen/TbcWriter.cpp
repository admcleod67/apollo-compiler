#include "apollo/codegen/TbcWriter.hpp"

#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>

namespace apollo::codegen {
namespace {

constexpr char kInstrIndent[] = "    ";

/// Keep the operand recognisable as a float rather than an integer: the VM parses with
/// `std::stod` either way, but `PUSH_FLT 1.0` reads better than `PUSH_FLT 1`.
std::string withDecimalPoint(std::string text) {
    if (text.find_first_of(".eEni") == std::string::npos) {
        text += ".0";
    }
    return text;
}

/// Shortest decimal spelling that reads back as the same `double`. The default stream
/// precision is 6 significant digits, which silently truncates real literals.
std::string formatDouble(double value) {
    for (int precision = 15; precision <= 17; ++precision) {
        std::ostringstream oss;
        oss << std::setprecision(precision) << value;
        const std::string text = oss.str();
        try {
            if (std::stod(text) == value) {
                return withDecimalPoint(text);
            }
        } catch (...) { // NaN / infinity spellings never round-trip through stod
            break;
        }
    }
    std::ostringstream oss;
    oss << std::setprecision(17) << value;
    return withDecimalPoint(oss.str());
}

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
    buffer_ << kInstrIndent << "PUSH_FLT " << formatDouble(value) << '\n';
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
