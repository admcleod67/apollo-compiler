#include "apollo/common/Listing.hpp"

#include <iomanip>
#include <ostream>

namespace apollo::common {

void writeListing(std::ostream &out, const SourceFile &source) {
    for (std::size_t line = 1; line <= source.lineCount(); ++line) {
        out << std::setw(4) << line << ": " << source.lineText(line) << '\n';
    }
}

} // namespace apollo::common
