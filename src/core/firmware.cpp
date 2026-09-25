// SPDX-License-Identifier: GPL-2.0-or-later
#include "firmware.h"

bool Firmware::isUefi() const
{
    return interfaceTypes.contains("uefi");
}

bool Firmware::hasSecureBoot() const
{
    return features.contains("secure-boot") && features.contains("enrolled-keys");
}

bool Firmware::requiresSmm() const
{
    return features.contains("requires-smm");
}

/* STUB: to be implemented */

namespace FirmwareDb {

QList<Firmware> list(const QStringList &)
{
    return {};
}

std::optional<Firmware> find(bool, const QString &, const QStringList &)
{
    return std::nullopt;
}

bool apply(ArgsFile &, const Firmware &, const QString &, QString *error)
{
    if (error) {
        *error = QStringLiteral("not implemented");
    }
    return false;
}

}
