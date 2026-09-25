// SPDX-License-Identifier: GPL-2.0-or-later
#include "vmrunner.h"

#include "core/paths.h"

/* STUB: to be implemented */

struct VmRunner::Private
{
    QString id;
    QString dir;
};

VmRunner::VmRunner(const QString &id, const QString &dir, QObject *parent)
    : QObject(parent), d(new Private{id, dir})
{
}

VmRunner::~VmRunner()
{
    delete d;
}

VmRunner::State VmRunner::state() const
{
    return State::Stopped;
}

bool VmRunner::isActive() const
{
    return false;
}

QString VmRunner::errorString() const
{
    return {};
}

QString VmRunner::logPath() const
{
    return d->dir + "/qemu.log";
}

QStringList VmRunner::commandLine(const ArgsFile &args) const
{
    return QStringList{Paths::qemuBinary()} + args.argv();
}

void VmRunner::start(const ArgsFile &)
{
    emit failed(tr("Starting VMs is not implemented yet"));
}

void VmRunner::attach()
{
}

void VmRunner::pause()
{
}

void VmRunner::resume()
{
}

void VmRunner::powerdown()
{
}

void VmRunner::reset()
{
}

void VmRunner::forceOff()
{
}

QmpClient *VmRunner::qmp() const
{
    return nullptr;
}
