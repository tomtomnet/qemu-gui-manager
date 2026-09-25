// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QHash>
#include <QObject>

#include "core/qemuinfo.h"

/*
 * The documentation of the QEMU the VMs run with, shared by the argument
 * editors and the reference windows, loaded in the background.
 */
class QemuDocs : public QObject
{
    Q_OBJECT

public:
    static QemuDocs *instance();

    /* Null until loaded */
    const QemuInfo *info() const;
    /* Why info() is null: loading, no binary, or an error */
    QString status() const { return m_status; }
    QString binary() const { return m_binary; }
    /* Follows Paths::qemuBinary(), after the preferences changed it */
    void reload();
    /* Emits propertiesLoaded() once info() has them */
    void loadProperties(const QString &device);

signals:
    void changed();
    void propertiesLoaded(const QString &device);

private:
    QemuDocs();

    QHash<QString, QemuInfoLoader *> m_loaders;
    QemuInfoLoader *m_loader = nullptr;
    QString m_binary;
    QString m_status;
};
