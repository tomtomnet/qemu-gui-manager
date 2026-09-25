// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

#include "core/argsfile.h"

/*
 * The firmware images the distribution describes in the standard QEMU
 * firmware descriptors, the JSON files in /usr/share/qemu/firmware and
 * friends (docs/interop/firmware.json in QEMU).
 */
struct Firmware
{
    QString descriptorPath;
    QString description;
    QStringList interfaceTypes;     // uefi, bios
    QString code;                   // mapping.executable.filename
    QString varsTemplate;           // mapping.nvram-template.filename, if any
    QString format;                 // raw or qcow2
    QString mode;                   // split, combined or stateless
    QStringList machines;           // x86_64 targets, e.g. pc-q35-*
    QStringList features;           // secure-boot, enrolled-keys, requires-smm...

    bool isUefi() const;
    /* secure-boot with the Microsoft keys enrolled */
    bool hasSecureBoot() const;
    bool requiresSmm() const;
};

namespace FirmwareDb {

/*
 * The x86_64 flash firmware in @dirs, by default /usr/share/qemu/firmware,
 * /etc/qemu/firmware and ~/.config/qemu/firmware: later directories
 * override files of the same name, and the result is in file name order,
 * which is the priority order.
 */
QList<Firmware> list(const QStringList &dirs = {});
/* The first UEFI firmware for @machine with or without secure boot */
std::optional<Firmware> find(bool secureBoot, const QString &machine = "pc-q35-10.0",
                             const QStringList &dirs = {});
/*
 * Makes @fw the firmware of @args: replaces its pflash drives (and -bios)
 * with those of @fw, copying the variable store template into @vmDir
 * (referenced relatively: QEMU runs in the VM folder) unless a copy is
 * already there, and for secure boot or SMM firmware sets smm=on and the
 * secure flash.
 */
bool apply(ArgsFile &args, const Firmware &fw, const QString &vmDir,
           QString *error = nullptr);

}
