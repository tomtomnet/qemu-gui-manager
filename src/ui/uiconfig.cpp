// SPDX-License-Identifier: GPL-2.0-or-later
#include "uiconfig.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>

#include "core/qemuinfo.h"

namespace UiConfig {

static QString tr(const char *text)
{
    return QCoreApplication::translate("UiConfig", text);
}

/* Inserts a -@name line after the last -@after line, else at the top */
static int insertAfter(ArgsFile &args, const QStringList &after, const QString &name,
                       const QString &value)
{
    ArgsFile::Line line;
    int at = 0;

    for (const QString &a : after) {
        const QList<int> found = args.indexesOf(a);
        if (!found.isEmpty()) {
            at = qMax(at, found.last() + 1);
        }
    }
    if (at == 0) {
        /* after the comments at the top */
        while (at < args.lines.size() && args.lines[at].kind == ArgsFile::Line::Comment) {
            at++;
        }
    }
    line.kind = ArgsFile::Line::Option;
    line.name = name;
    line.value = value;
    args.lines.insert(at, line);
    return at;
}

QString machineType(const ArgsFile &args)
{
    QString type;

    for (int i : args.indexesOf("machine")) {
        const OptionValue v = args.valueAt(i);
        if (!v.implied().isEmpty()) {
            type = v.implied();
        } else if (v.has("type")) {
            type = v.get("type");
        }
    }
    return type;
}

void setMachineType(ArgsFile &args, const QString &type)
{
    const QList<int> machines = args.indexesOf("machine");
    int target = -1;

    for (int i : machines) {
        const OptionValue v = args.valueAt(i);
        if (!v.implied().isEmpty() || v.has("type")) {
            target = i;
        }
    }
    if (target < 0 && !machines.isEmpty()) {
        target = machines.first();
    }
    if (target < 0) {
        if (!type.isEmpty()) {
            insertAfter(args, {"name"}, "machine", type);
        }
        return;
    }

    OptionValue v = args.valueAt(target);
    if (v.implied().isEmpty() && v.has("type")) {
        v.set("type", type);
    } else {
        v.setImplied(type);
    }
    args.setValueAt(target, v);
}

QString accel(const ArgsFile &args)
{
    const int a = args.indexOf("accel");
    QString value;

    if (a >= 0) {
        const OptionValue v = args.valueAt(a);
        return v.implied().isEmpty() ? v.get("accel") : v.implied();
    }
    for (int i : args.indexesOf("machine")) {
        const OptionValue v = args.valueAt(i);
        if (v.has("accel")) {
            value = v.get("accel");
        }
    }
    if (value.isEmpty() && args.indexOf("enable-kvm") >= 0) {
        value = "kvm";
    }
    return value;
}

void setAccel(ArgsFile &args, const QString &accel)
{
    const int a = args.indexOf("accel");

    if (a >= 0) {
        OptionValue v = args.valueAt(a);
        if (v.implied().isEmpty() && v.has("accel")) {
            v.set("accel", accel);
        } else {
            v.setImplied(accel);
        }
        args.setValueAt(a, v);
        return;
    }
    for (int i : args.indexesOf("machine")) {
        OptionValue v = args.valueAt(i);
        if (v.has("accel")) {
            v.set("accel", accel);
            args.setValueAt(i, v);
            return;
        }
    }
    if (args.indexOf("enable-kvm") >= 0) {
        if (accel == "kvm") {
            return;
        }
        args.removeAll("enable-kvm");
    }
    if (!accel.isEmpty()) {
        insertAfter(args, {"name", "machine"}, "accel", accel);
    }
}

static bool secureFlash(const ArgsFile &args)
{
    for (int i : args.indexesOf("global")) {
        const OptionValue v = args.valueAt(i);
        if ((v.get("driver") == "cfi.pflash01" && v.get("property") == "secure" &&
             v.flag("value")) ||
            v.flag("cfi.pflash01.secure")) {
            return true;
        }
    }
    return false;
}

QString firmwareSummary(const ArgsFile &args)
{
    QString code;
    bool uefi = false;

    for (int i : args.indexesOf("drive")) {
        const OptionValue v = args.valueAt(i);
        if (v.get("if") != "pflash") {
            continue;
        }
        uefi = true;
        /* the code, not the variable store */
        if (code.isEmpty() || v.get("unit") == "0" || v.flag("readonly")) {
            code = QFileInfo(v.get("file")).fileName();
        }
    }
    for (int i : args.indexesOf("machine")) {
        uefi |= args.valueAt(i).has("pflash0");
    }
    if (uefi) {
        const QString name = secureFlash(args) ? tr("UEFI with Secure Boot") : tr("UEFI");
        return code.isEmpty() ? name : QString("%1 (%2)").arg(name, code);
    }

    const int bios = args.indexOf("bios");
    if (bios >= 0) {
        return tr("Firmware file %1").arg(QFileInfo(args.lines[bios].value).fileName());
    }
    return tr("BIOS (SeaBIOS)");
}

QList<Disk> disks(const ArgsFile &args)
{
    static const QStringList hd = {"hda", "hdb", "hdc", "hdd"};
    QList<Disk> list;

    for (int i = 0; i < args.lines.size(); i++) {
        const ArgsFile::Line &line = args.lines[i];
        if (line.kind != ArgsFile::Line::Option) {
            continue;
        }
        const OptionValue v(line.value);
        Disk d;

        if (line.name == "drive") {
            if (v.get("if") == "pflash") {
                continue;
            }
            d.file = v.get("file");
            d.cdrom = v.get("media") == "cdrom";
            d.interface = v.get("if");
            if (d.interface == "none" && v.has("id")) {
                /* the device that uses the drive */
                d.interface.clear();
                for (int j : args.indexesOf("device")) {
                    const OptionValue dev = args.valueAt(j);
                    if (dev.get("drive") == v.get("id")) {
                        d.interface = dev.implied();
                        d.cdrom |= dev.implied().endsWith("-cd");
                    }
                }
            }
        } else if (hd.contains(line.name)) {
            d.file = line.value;
            d.interface = "ide";
        } else if (line.name == "cdrom") {
            d.file = line.value;
            d.cdrom = true;
            d.interface = "ide";
        } else if (line.name == "blockdev" && v.has("filename")) {
            d.file = v.get("filename");
        } else {
            continue;
        }
        list << d;
    }
    return list;
}

QStringList devicesOf(const ArgsFile &args, const QString &category,
                      const QemuInfo *info)
{
    static const QHash<QString, QStringList> known = {
        {"Display devices",
         {"VGA", "virtio-vga", "virtio-vga-gl", "virtio-gpu-pci", "virtio-gpu-gl-pci",
          "virtio-gpu", "virtio-gpu-gl", "qxl", "qxl-vga", "cirrus-vga", "bochs-display",
          "ramfb", "vmware-svga", "secondary-vga", "ati-vga", "vhost-user-gpu",
          "vhost-user-vga", "virtio-vga-rutabaga", "virtio-gpu-rutabaga-pci"}},
        {"Network devices",
         {"virtio-net-pci", "virtio-net", "e1000", "e1000e", "igb", "rtl8139", "vmxnet3",
          "ne2k_pci", "pcnet", "usb-net", "i82559er", "virtio-net-pci-non-transitional"}},
        {"Sound devices",
         {"intel-hda", "ich9-intel-hda", "hda-duplex", "hda-output", "hda-micro", "AC97",
          "ES1370", "sb16", "usb-audio", "virtio-sound-pci", "virtio-sound"}},
    };
    QStringList found;

    for (int i : args.indexesOf("device")) {
        const QString driver = args.valueAt(i).implied();
        const QemuDeviceDoc *doc = info ? info->device(driver) : nullptr;

        if (doc ? doc->category == category : known.value(category).contains(driver)) {
            found << driver;
        }
    }
    return found;
}

QString displaySummary(const ArgsFile &args, const QemuInfo *info)
{
    static const QHash<QString, const char *> outputs = {
        {"sdl", QT_TRANSLATE_NOOP("UiConfig", "SDL window")},
        {"gtk", QT_TRANSLATE_NOOP("UiConfig", "GTK window")},
        {"none", QT_TRANSLATE_NOOP("UiConfig", "no window")},
        {"egl-headless", QT_TRANSLATE_NOOP("UiConfig", "headless")},
        {"dbus", QT_TRANSLATE_NOOP("UiConfig", "D-Bus")},
        {"spice-app", QT_TRANSLATE_NOOP("UiConfig", "SPICE viewer")},
        {"curses", QT_TRANSLATE_NOOP("UiConfig", "text console")},
    };
    QStringList devices = devicesOf(args, "Display devices", info);
    const int vga = args.indexOf("vga");
    const int display = args.indexOf("display");
    QString output;

    if (vga >= 0 && args.lines[vga].value != "none") {
        devices << args.lines[vga].value;
    }
    if (args.indexOf("nographic") >= 0) {
        output = tr("no window (serial console)");
    } else if (display >= 0) {
        const OptionValue v = args.valueAt(display);
        const QString type = v.implied();
        const QString gl = v.get("gl", "off");

        output = outputs.contains(type) ? tr(outputs.value(type)) : type;
        if (gl != "off") {
            output = tr("%1 with OpenGL").arg(output);
        }
    } else {
        output = tr("default window");
    }
    if (devices.isEmpty()) {
        return output;
    }
    return QString("%1 · %2").arg(devices.join(", "), output);
}

QString networkSummary(const ArgsFile &args, const QemuInfo *info)
{
    static const QHash<QString, const char *> types = {
        {"user", QT_TRANSLATE_NOOP("UiConfig", "NAT (user networking)")},
        {"passt", QT_TRANSLATE_NOOP("UiConfig", "NAT (passt)")},
        {"tap", QT_TRANSLATE_NOOP("UiConfig", "TAP interface")},
        {"bridge", QT_TRANSLATE_NOOP("UiConfig", "bridge")},
    };
    auto typeName = [](const QString &type) {
        return types.contains(type) ? tr(types.value(type)) : type;
    };
    QStringList parts;

    for (int i : args.indexesOf("netdev")) {
        const OptionValue v = args.valueAt(i);
        QString model;

        for (int j : args.indexesOf("device")) {
            const OptionValue dev = args.valueAt(j);
            if (dev.get("netdev") == v.get("id") && !v.get("id").isEmpty()) {
                model = dev.implied();
            }
        }
        parts << (model.isEmpty() ? typeName(v.implied())
                                  : QString("%1 · %2").arg(typeName(v.implied()), model));
    }
    for (int i : args.indexesOf("nic")) {
        const OptionValue v = args.valueAt(i);
        if (v.implied() == "none") {
            parts << tr("not connected");
        } else {
            const QString model = v.get("model");
            parts << (model.isEmpty() ? typeName(v.implied())
                                      : QString("%1 · %2").arg(typeName(v.implied()), model));
        }
    }
    if (!args.indexesOf("net").isEmpty()) {
        parts << tr("legacy -net options");
    }
    if (parts.isEmpty()) {
        const QStringList cards = devicesOf(args, "Network devices", info);
        if (!cards.isEmpty()) {
            return cards.join(", ");
        }
        return args.indexOf("nodefaults") >= 0 ? tr("none")
                                               : tr("QEMU default (NAT)");
    }
    return parts.join("; ");
}

QString audioSummary(const ArgsFile &args, const QemuInfo *info)
{
    static const QHash<QString, QString> drivers = {
        {"pipewire", "PipeWire"}, {"pa", "PulseAudio"}, {"alsa", "ALSA"},
        {"jack", "JACK"},         {"sdl", "SDL"},       {"oss", "OSS"},
    };
    QStringList backends;
    QStringList devices = devicesOf(args, "Sound devices", info);

    for (int i : args.indexesOf("audiodev")) {
        const QString type = args.valueAt(i).implied();
        backends << drivers.value(type, type);
    }
    for (int i : args.indexesOf("audio")) {
        const OptionValue v = args.valueAt(i);
        const QString type = v.implied().isEmpty() ? v.get("driver") : v.implied();
        backends << drivers.value(type, type);
        if (v.has("model")) {
            devices << v.get("model");
        }
    }
    if (backends.isEmpty() && devices.isEmpty()) {
        return tr("none");
    }
    if (devices.isEmpty()) {
        return backends.join(", ");
    }
    return QString("%1 · %2").arg(backends.join(", "), devices.join(", "));
}

QString shellQuote(const QString &arg)
{
    static const QRegularExpression safe("^[A-Za-z0-9_@%+=:,./-]+$");

    if (safe.match(arg).hasMatch()) {
        return arg;
    }
    QString quoted = arg;
    return '\'' + quoted.replace('\'', "'\\''") + '\'';
}

QString commandText(const QStringList &command, const QString &dir)
{
    QStringList lines;
    QString current;

    for (qsizetype i = 0; i < command.size(); i++) {
        const QString &arg = command[i];
        if (i == 0) {
            current = shellQuote(arg);
        } else if (arg.startsWith('-') && arg.size() > 1) {
            lines << current;
            current = "    " + shellQuote(arg);
        } else {
            current += ' ' + shellQuote(arg);
        }
    }
    lines << current;
    return "cd " + shellQuote(dir) + "\n" + lines.join(" \\\n") + "\n";
}

}
