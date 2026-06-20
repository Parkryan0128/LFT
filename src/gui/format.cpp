#include "gui/format.h"

#include <QString>

namespace lft::gui {

QString formatBytes(quint64 n) {
    if (n < 1024) {
        return QString::number(n) + " B";
    }
    const char* units[] = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(n);
    int unit = -1;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    return QString::number(value, 'f', 1) + ' ' + units[unit];
}

}  // namespace lft::gui
