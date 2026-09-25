// SPDX-License-Identifier: GPL-2.0-or-later
#include "vmtemplate.h"

#include <QFileInfo>

#include "core/vmconfig.h"

namespace VmTemplate {

Defaults defaults(Os os)
{
    switch (os) {
    case Os::Linux:
        return {4096, 4, 64, Firmware::Uefi, Graphics::Accelerated};
    case Os::Windows11:
        return {8192, 4, 128, Firmware::UefiSecureBoot, Graphics::Standard};
    case Os::Windows:
        return {4096, 2, 64, Firmware::Uefi, Graphics::Standard};
    case Os::Other:
        break;
    }
    return {2048, 2, 32, Firmware::Uefi, Graphics::Compatible};
}

QString diskFormat(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();

    if (suffix == "qcow2" || suffix == "vdi" || suffix == "vmdk" || suffix == "vhdx") {
        return suffix;
    }
    if (suffix == "vhd") {
        return "vpc";
    }
    if (suffix == "img" || suffix == "raw") {
        return "raw";
    }
    return {};
}

ArgsFile build(const Options &o, const std::function<void(ArgsFile &)> &addFirmware)
{
    const bool windows = o.os == Os::Windows11 || o.os == Os::Windows;
    const bool virtio = o.os == Os::Linux;
    ArgsFile args;
    qsizetype sectionStart = 0;

    auto comment = [&](const QString &text) {
        ArgsFile::Line line;
        line.kind = ArgsFile::Line::Comment;
        line.text = text;
        args.lines << line;
    };
    auto option = [&](const QString &name, const QString &value = {}) {
        ArgsFile::Line line;
        line.kind = ArgsFile::Line::Option;
        line.name = name;
        line.value = value;
        args.lines << line;
    };
    auto section = [&](const QString &title) {
        args.lines << ArgsFile::Line();
        sectionStart = args.lines.size();
        comment("# " + title);
    };

    comment("# The QEMU command line of this VM, one option per line.");
    comment("# Lines starting with # are comments; #share lines are shared folders.");
    args.lines << ArgsFile::Line();
    VmConfig::setName(args, o.name);

    section("System");
    option("machine", "q35,memory-backend=mem");
    option("accel", "kvm");
    option("cpu", windows ? "host,hv-relaxed,hv-vapic,hv-spinlocks=0x1fff,hv-vpindex,"
                            "hv-runtime,hv-time,hv-synic,hv-stimer,hv-frequencies,"
                            "hv-tlbflush,hv-ipi"
                          : "host");
    option("smp", QString::number(o.cpus));
    /* memfd RAM, which virtiofsd can map to share folders */
    option("object", QString("memory-backend-memfd,id=mem,size=%1,share=on")
                         .arg(VmConfig::formatMiB(o.memoryMiB)));
    if (windows) {
        option("rtc", "base=localtime");
    }

    section("Firmware");
    if (addFirmware) {
        addFirmware(args);
    }
    if (args.lines.size() == sectionStart + 1) {
        /* BIOS: nothing to say */
        args.lines.resize(sectionStart - 1);
    }

    section("Display");
    switch (o.graphics) {
    case Graphics::Accelerated:
        option("device", "virtio-vga-gl");
        break;
    case Graphics::Standard:
        option("device", "virtio-vga");
        break;
    case Graphics::Compatible:
        option("device", "VGA");
        break;
    }
    option("display", "sdl,gl=on");

    if (!o.disk.isEmpty() || !o.iso.isEmpty()) {
        section("Storage");
        if (!o.disk.isEmpty()) {
            const QString format = diskFormat(o.disk);
            QString value = "file=" + OptionValue::escape(o.disk);

            if (!format.isEmpty()) {
                value += ",format=" + format;
            }
            value += virtio ? ",if=virtio" : ",if=ide";
            value += ",discard=unmap";
            option("drive", value);
        }
        if (!o.iso.isEmpty()) {
            option("drive", "file=" + OptionValue::escape(o.iso) + ",media=cdrom,readonly=on");
        }
    }

    section("Network");
    option("netdev", "user,id=net0");
    option("device", QString(virtio ? "virtio-net-pci" : "e1000e") + ",netdev=net0");

    section("Sound");
    option("audiodev", "pipewire,id=audio0");
    option("device", "ich9-intel-hda");
    option("device", "hda-duplex,audiodev=audio0");

    section("USB");
    option("device", "qemu-xhci");
    option("device", "usb-tablet");

    section("Clipboard sharing, with spice-vdagent in the guest");
    option("device", "virtio-serial-pci");
    option("chardev", "qemu-vdagent,id=vdagent0,name=vdagent,clipboard=on,mouse=off");
    option("device", "virtserialport,chardev=vdagent0,name=com.redhat.spice.0");
    return args;
}

}
