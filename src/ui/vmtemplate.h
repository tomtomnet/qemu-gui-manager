// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>

#include <functional>

#include "core/argsfile.h"

/*
 * The arguments of a new VM: a modern machine with KVM, virtio devices for
 * Linux and devices Windows has drivers for, shared memory for virtiofs,
 * and the clipboard channel of spice-vdagent.
 */
namespace VmTemplate {

enum class Os { Linux, Windows11, Windows, Other };
enum class Firmware { Uefi, UefiSecureBoot, Bios };
enum class Graphics { Accelerated, Standard, Compatible };

struct Options {
    QString name;
    Os os = Os::Linux;
    qint64 memoryMiB = 4096;
    int cpus = 4;
    /* Relative to the VM folder or absolute; empty for none */
    QString disk;
    QString iso;
    Graphics graphics = Graphics::Accelerated;
};

struct Defaults {
    qint64 memoryMiB;
    int cpus;
    int diskGiB;
    Firmware firmware;
    Graphics graphics;
};
Defaults defaults(Os os);

/* The disk format for the file name, empty when unknown */
QString diskFormat(const QString &path);

/*
 * The arguments, in sections.  @addFirmware is called where the firmware
 * section goes, to append its lines (FirmwareDb::apply does); a section
 * left empty is dropped.
 */
ArgsFile build(const Options &options,
               const std::function<void(ArgsFile &)> &addFirmware = {});

}
