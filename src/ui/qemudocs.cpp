// SPDX-License-Identifier: GPL-2.0-or-later
#include "qemudocs.h"

#include <QCoreApplication>
#include <QFileInfo>

#include "core/paths.h"

QemuDocs *QemuDocs::instance()
{
    static QemuDocs *docs = nullptr;

    if (!docs) {
        docs = new QemuDocs;
        docs->reload();
    }
    return docs;
}

QemuDocs::QemuDocs() : QObject(QCoreApplication::instance())
{
}

const QemuInfo *QemuDocs::info() const
{
    return m_loader && m_loader->isLoaded() ? &m_loader->info() : nullptr;
}

void QemuDocs::reload()
{
    const QString binary = Paths::qemuBinary();

    if (m_loader && binary == m_binary) {
        return;
    }
    m_binary = binary;
    m_loader = nullptr;
    if (binary.isEmpty()) {
        m_status = tr("QEMU was not found. Set its path in the preferences.");
        emit changed();
        return;
    }
    if (!QFileInfo(binary).isExecutable()) {
        m_status = tr("%1 is not an executable. Check the QEMU path in the preferences.")
                       .arg(binary);
        emit changed();
        return;
    }

    m_loader = m_loaders.value(binary);
    if (!m_loader) {
        QemuInfoLoader *loader = new QemuInfoLoader(binary, this);

        m_loaders.insert(binary, loader);
        connect(loader, &QemuInfoLoader::loaded, this, [this, loader]() {
            if (loader == m_loader) {
                m_status.clear();
                emit changed();
            }
        });
        connect(loader, &QemuInfoLoader::failed, this, [this, loader](const QString &error) {
            if (loader == m_loader) {
                m_status = tr("Cannot read the QEMU documentation: %1").arg(error);
                emit changed();
            }
        });
        connect(loader, &QemuInfoLoader::propertiesLoaded, this,
                [this, loader](const QString &device) {
            if (loader == m_loader) {
                emit propertiesLoaded(device);
            }
        });
        m_loader = loader;
    }
    if (m_loader->isLoaded()) {
        m_status.clear();
        emit changed();
        return;
    }
    m_status = tr("Loading the QEMU documentation…");
    emit changed();
    /* may emit loaded() right away, from the cache */
    m_loader->load();
}

void QemuDocs::loadProperties(const QString &device)
{
    if (m_loader && m_loader->isLoaded()) {
        m_loader->loadProperties(device);
    }
}
