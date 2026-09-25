// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "core/argsfile.h"

struct QemuInfo;

/*
 * More settings read from and written into the arguments, for the pages
 * and the summary, beyond what VmConfig offers.
 */
namespace UiConfig {

/* The type of the last -machine that has one, e.g. q35 */
QString machineType(const ArgsFile &args);
void setMachineType(ArgsFile &args, const QString &type);

/* -accel, -machine accel= or -enable-kvm; empty when unset (TCG) */
QString accel(const ArgsFile &args);
void setAccel(ArgsFile &args, const QString &accel);

/* e.g. "UEFI (OVMF_CODE_4M.qcow2)", "BIOS" */
QString firmwareSummary(const ArgsFile &args);

struct Disk {
    QString file;
    bool cdrom = false;
    QString interface;      // virtio, ide, ...; empty when unknown
};
QList<Disk> disks(const ArgsFile &args);

/* The drivers of the -device lines in @category (e.g. "Display devices"),
   by the QEMU documentation when loaded, else by known names */
QStringList devicesOf(const ArgsFile &args, const QString &category,
                      const QemuInfo *info);
QString displaySummary(const ArgsFile &args, const QemuInfo *info);
QString networkSummary(const ArgsFile &args, const QemuInfo *info);
QString audioSummary(const ArgsFile &args, const QemuInfo *info);

/* For a shell */
QString shellQuote(const QString &arg);
/* cd DIR, then the command with one option per line */
QString commandText(const QStringList &command, const QString &dir);

}
