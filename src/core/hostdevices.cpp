// SPDX-License-Identifier: GPL-2.0-or-later
#include "hostdevices.h"

QString PciDevice::displayName() const
{
    return QString("%1 %2 %3").arg(address.mid(5), vendorName, deviceName).simplified();
}

QString UsbDevice::devNode() const
{
    return QString::asprintf("/dev/bus/usb/%03d/%03d", bus, device);
}

QString UsbDevice::displayName() const
{
    return QString("%1 %2 (%3:%4)")
        .arg(manufacturer, product)
        .arg(vendorId, 4, 16, QChar('0'))
        .arg(productId, 4, 16, QChar('0'))
        .simplified();
}

/* STUB: to be implemented */

namespace HostDevices {

bool iommuEnabled(const QString &)
{
    return false;
}

QList<PciDevice> pciDevices(const QString &)
{
    return {};
}

QList<UsbDevice> usbDevices(const QString &)
{
    return {};
}

QStringList pciProblems(const PciDevice &, qint64, const QString &)
{
    return {QStringLiteral("not implemented")};
}

qint64 memlockLimit()
{
    return -1;
}

}
