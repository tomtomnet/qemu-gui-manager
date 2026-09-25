// SPDX-License-Identifier: GPL-2.0-or-later
#include "icons.h"

#include <QApplication>

namespace Icons {

QIcon themed(const QStringList &names, QStyle::StandardPixmap fallback)
{
    for (const QString &name : names) {
        if (QIcon::hasThemeIcon(name)) {
            return QIcon::fromTheme(name);
        }
    }
    return qApp->style()->standardIcon(fallback);
}

QIcon app()
{
    return QIcon::fromTheme("qemu-gui-manager", QIcon(":/icons/qemu-gui-manager.svg"));
}

}
