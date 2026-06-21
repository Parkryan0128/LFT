#include "lft/format.h"

#include <iomanip>
#include <sstream>

namespace lft {

std::string format_bytes(uint64_t n) {
    if (n < 1024) {
        return std::to_string(n) + " B";
    }
    const char* units[] = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(n);
    int unit = -1;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream os;
    os << std::fixed << std::setprecision(1) << value << ' ' << units[unit];
    return os.str();
}

}  // namespace lft
