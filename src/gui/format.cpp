#include "gui/format.h"

#include "lft/format.h"

#include <QString>

namespace lft::gui {

QString formatBytes(quint64 bytes) {
    return QString::fromStdString(lft::format_bytes(bytes));
}

}  // namespace lft::gui
