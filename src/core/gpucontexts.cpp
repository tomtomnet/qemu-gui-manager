// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpucontexts.h"

#include <QJsonObject>

#include "core/qmpclient.h"

/* Where the devices of the command line are: with an id, and without */
static const char *const kParents[] = {"/machine/peripheral", "/machine/peripheral-anon"};

GpuContexts::GpuContexts(QObject *parent) : QObject(parent)
{
}

QString GpuContexts::findGpu(const QJsonArray &children, const QString &parent)
{
    /* the PCI and VGA forms pass the properties of the GPU on */
    static const QStringList types{"child<virtio-vga-gl>", "child<virtio-gpu-gl-pci>",
                                   "child<virtio-gpu-gl-device>"};

    for (const QJsonValue &child : children) {
        if (types.contains(child["type"].toString())) {
            return parent + '/' + child["name"].toString();
        }
    }
    return {};
}

GpuContexts::Status GpuContexts::statusOf(bool offered, qint64 drmContexts,
                                          qint64 virglContexts)
{
    if (!offered) {
        return Status::NotOffered;
    }
    if (drmContexts > 0) {
        return Status::InUse;
    }
    return virglContexts > 0 ? Status::Virgl : Status::Waiting;
}

void GpuContexts::reset()
{
    m_path.clear();
    m_status = Status::Unknown;
    m_busy = false;
}

void GpuContexts::update(QmpClient *qmp)
{
    if (m_busy || !qmp || !qmp->isReady()) {
        return;
    }
    m_busy = true;
    m_qmp = qmp;
    if (m_path.isEmpty()) {
        findGpu(0);
    } else {
        readCounts();
    }
}

void GpuContexts::findGpu(int parent)
{
    if (parent >= int(std::size(kParents))) {
        finish(Status::Unknown);       // no 3D GPU
        return;
    }
    m_qmp->execute("qom-list", {{"path", kParents[parent]}},
                   [this, parent, guard = QPointer<GpuContexts>(this)](const QJsonValue &result,
                                                                       const QString &error) {
        if (!guard) {
            return;
        }
        m_path = error.isEmpty() ? findGpu(result.toArray(), kParents[parent]) : QString();
        if (m_path.isEmpty()) {
            findGpu(parent + 1);
        } else if (m_qmp) {
            readCounts();
        } else {
            finish(Status::Unknown);
        }
    });
}

void GpuContexts::readCounts()
{
    auto get = [this](const char *property, const QmpClient::Callback &done) {
        m_qmp->execute("qom-get", {{"path", m_path}, {"property", property}}, done);
    };
    const QPointer<GpuContexts> guard(this);

    /* a QEMU without the counts has no x-drm-offered: nothing to tell then */
    get("x-drm-offered", [=, this](const QJsonValue &offered, const QString &error) {
        if (!guard || !m_qmp) {
            return;
        }
        if (!error.isEmpty()) {
            finish(Status::Unknown);
            return;
        }
        get("x-drm-contexts", [=, this](const QJsonValue &drm, const QString &error) {
            if (!guard || !m_qmp) {
                return;
            }
            if (!error.isEmpty()) {
                finish(Status::Unknown);
                return;
            }
            get("x-virgl-contexts", [=, this](const QJsonValue &virgl, const QString &error) {
                if (!guard) {
                    return;
                }
                finish(error.isEmpty() ? statusOf(offered.toBool(), drm.toInteger(),
                                                  virgl.toInteger())
                                       : Status::Unknown);
            });
        });
    });
}

void GpuContexts::finish(Status status)
{
    const bool changing = status != m_status;

    m_busy = false;
    m_status = status;
    if (changing) {
        emit changed();
    }
}
