// SPDX-License-Identifier: GPL-2.0-or-later
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "core/firmware.h"

class TestFirmware : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir tmp;
    QStringList dirs;

    QString touch(const QString &name, const QByteArray &data = "x")
    {
        const QString path = tmp.filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(data);
        }
        return path;
    }

    void descriptor(const QString &dir, const QString &name, const QString &description,
                    const QString &device, const QStringList &machines,
                    const QStringList &features, const QString &code = {},
                    const QString &vars = {}, const QString &format = "qcow2")
    {
        QJsonObject mapping{{"device", device}};
        if (device == "flash") {
            mapping["mode"] = vars.isEmpty() ? "stateless" : "split";
            mapping["executable"] = QJsonObject{{"filename", code}, {"format", format}};
            if (!vars.isEmpty()) {
                mapping["nvram-template"] = QJsonObject{{"filename", vars}, {"format", format}};
            }
        } else {
            mapping["filename"] = code;
        }
        const QJsonObject root{
            {"description", description},
            {"interface-types", QJsonArray{"uefi"}},
            {"mapping", mapping},
            {"targets", QJsonArray{QJsonObject{{"architecture", "x86_64"},
                                               {"machines", QJsonArray::fromStringList(machines)}}}},
            {"features", QJsonArray::fromStringList(features)},
        };
        touch(dir + '/' + name, QJsonDocument(root).toJson());
    }

private slots:
    void initTestCase()
    {
        const QString code = touch("ovmf/CODE.qcow2");
        const QString sbCode = touch("ovmf/CODE.secboot.qcow2");
        const QString vars = touch("ovmf/VARS.qcow2", "vars");
        const QString sbVars = touch("ovmf/VARS.secboot.qcow2", "secure vars");

        descriptor("share", "30-sb-enrolled.json", "SB enrolled", "flash", {"pc-q35-*"},
                   {"acpi-s3", "enrolled-keys", "requires-smm", "secure-boot"}, sbCode, sbVars);
        descriptor("share", "40-sb.json", "SB without keys", "flash", {"pc-q35-*"},
                   {"requires-smm", "secure-boot"}, sbCode, sbVars);
        descriptor("share", "50-nosb.json", "plain, overridden", "flash",
                   {"pc-i440fx-*", "pc-q35-*"}, {"amd-sev"}, code, vars);
        descriptor("etc", "50-nosb.json", "plain", "flash", {"pc-i440fx-*", "pc-q35-*"},
                   {"acpi-s3", "amd-sev", "amd-sev-es"}, code, vars);
        descriptor("share", "55-microvm.json", "microvm flash", "flash", {"microvm"}, {},
                   code, vars);
        descriptor("share", "60-stateless.json", "in memory", "memory", {"pc-q35-*"}, {},
                   code);
        descriptor("share", "70-masked.json", "masked", "flash", {"pc-q35-*"}, {}, code,
                   vars);
        touch("user/70-masked.json", "");
        dirs = {tmp.filePath("share"), tmp.filePath("etc"), tmp.filePath("user")};
    }

    void listByNameWithOverrides()
    {
        const QList<Firmware> list = FirmwareDb::list(dirs);
        QStringList names;

        for (const Firmware &fw : list) {
            names << QFileInfo(fw.descriptorPath).fileName();
        }
        QCOMPARE(names, QStringList({"30-sb-enrolled.json", "40-sb.json", "50-nosb.json",
                                     "55-microvm.json"}));
        QCOMPARE(list[2].description, "plain");
        QVERIFY(list[2].descriptorPath.contains("/etc/"));
        QCOMPARE(list[2].format, "qcow2");
        QCOMPARE(list[2].mode, "split");
        QCOMPARE(list[2].code, tmp.filePath("ovmf/CODE.qcow2"));
        QCOMPARE(list[2].varsTemplate, tmp.filePath("ovmf/VARS.qcow2"));
        QCOMPARE(list[2].machines, QStringList({"pc-i440fx-*", "pc-q35-*"}));
        QVERIFY(list[2].isUefi());
        QVERIFY(!list[2].hasSecureBoot());
        QVERIFY(list[0].hasSecureBoot());
        QVERIFY(list[0].requiresSmm());
        QVERIFY(!list[1].hasSecureBoot());     /* no keys enrolled */
    }

    void find()
    {
        std::optional<Firmware> fw = FirmwareDb::find(false, "pc-q35-10.0", dirs);
        QVERIFY(fw);
        QCOMPARE(QFileInfo(fw->descriptorPath).fileName(), "50-nosb.json");

        fw = FirmwareDb::find(true, "q35", dirs);
        QVERIFY(fw);
        QCOMPARE(QFileInfo(fw->descriptorPath).fileName(), "30-sb-enrolled.json");

        fw = FirmwareDb::find(false, "pc", dirs);
        QVERIFY(fw);
        QCOMPARE(QFileInfo(fw->descriptorPath).fileName(), "50-nosb.json");

        fw = FirmwareDb::find(false, "microvm", dirs);
        QVERIFY(fw);
        QCOMPARE(QFileInfo(fw->descriptorPath).fileName(), "55-microvm.json");

        QVERIFY(!FirmwareDb::find(true, "pc-i440fx-9.2", dirs));
    }

    void applySecureBoot()
    {
        QTemporaryDir vm;
        ArgsFile args = ArgsFile::parse("-name test\n-machine q35,accel=kvm\n-m 4G\n");
        const Firmware fw = *FirmwareDb::find(true, "q35", dirs);
        QString error;

        QVERIFY2(FirmwareDb::apply(args, fw, vm.path(), &error), qPrintable(error));
        QCOMPARE(args.toText(),
                 "-name test\n"
                 "-machine q35,accel=kvm,smm=on\n"
                 "-m 4G\n"
                 "-drive if=pflash,format=qcow2,unit=0,readonly=on,file=" +
                     tmp.filePath("ovmf/CODE.secboot.qcow2") + "\n"
                 "-drive if=pflash,format=qcow2,unit=1,file=VARS.secboot.qcow2\n"
                 "-global driver=cfi.pflash01,property=secure,value=on\n");

        QFile copy(vm.filePath("VARS.secboot.qcow2"));
        QVERIFY(copy.open(QIODevice::ReadWrite));
        QCOMPARE(copy.readAll(), "secure vars");
        copy.close();

        /* switching firmware replaces the drives, keeps the variables */
        const Firmware plain = *FirmwareDb::find(false, "q35", dirs);
        QVERIFY(FirmwareDb::apply(args, plain, vm.path(), &error));
        QCOMPARE(args.toText(),
                 "-name test\n"
                 "-machine q35,accel=kvm,smm=on\n"
                 "-m 4G\n"
                 "-drive if=pflash,format=qcow2,unit=0,readonly=on,file=" +
                     tmp.filePath("ovmf/CODE.qcow2") + "\n"
                 "-drive if=pflash,format=qcow2,unit=1,file=VARS.qcow2\n");
        QVERIFY(QFileInfo::exists(vm.filePath("VARS.secboot.qcow2")));
        QVERIFY(QFileInfo::exists(vm.filePath("VARS.qcow2")));
    }

    void applyKeepsExistingVarsAndOtherDrives()
    {
        QTemporaryDir vm;
        ArgsFile args = ArgsFile::parse("-drive file=disk.qcow2,if=virtio\n"
                                        "-bios /old/bios.bin\n");
        const Firmware fw = *FirmwareDb::find(true, "q35", dirs);
        QFile vars(vm.filePath("VARS.secboot.qcow2"));

        QVERIFY(vars.open(QIODevice::WriteOnly));
        vars.write("my keys");
        vars.close();
        QVERIFY(FirmwareDb::apply(args, fw, vm.path()));
        QVERIFY(vars.open(QIODevice::ReadOnly));
        QCOMPARE(vars.readAll(), "my keys");
        QCOMPARE(args.indexOf("bios"), -1);
        QCOMPARE(args.lines[0].value, "file=disk.qcow2,if=virtio");
        QCOMPARE(args.indexesOf("drive").size(), 3);
        /* no -machine: q35 is what secure boot descriptors are for */
        QCOMPARE(args.lines[args.indexOf("machine")].value, "q35,smm=on");
    }

    void applyEscapesCommas()
    {
        QTemporaryDir vm;
        ArgsFile args;
        Firmware fw;

        fw.interfaceTypes = {"uefi"};
        fw.code = "/opt/fw,1/CODE.fd";
        fw.format = "raw";
        fw.mode = "stateless";
        fw.machines = {"pc-q35-*"};
        QVERIFY(FirmwareDb::apply(args, fw, vm.path()));
        QCOMPARE(args.toText(),
                 "-drive if=pflash,format=raw,unit=0,readonly=on,file=/opt/fw,,1/CODE.fd\n");
    }

    void missingTemplate()
    {
        QTemporaryDir vm;
        ArgsFile args;
        Firmware fw = *FirmwareDb::find(false, "q35", dirs);
        QString error;

        fw.varsTemplate = tmp.filePath("ovmf/nothing.qcow2");
        QVERIFY(!FirmwareDb::apply(args, fw, vm.path(), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(args.lines.isEmpty());
    }

    void system()
    {
        if (!QFileInfo::exists("/usr/share/qemu/firmware")) {
            QSKIP("no firmware descriptors installed");
        }
        QVERIFY(!FirmwareDb::list().isEmpty());
        const std::optional<Firmware> plain = FirmwareDb::find(false);
        const std::optional<Firmware> secure = FirmwareDb::find(true);
        QVERIFY(plain && plain->isUefi() && !plain->features.contains("secure-boot"));
        QVERIFY(QFileInfo::exists(plain->code));
        QVERIFY(QFileInfo::exists(plain->varsTemplate));
        QVERIFY(secure && secure->hasSecureBoot());
    }
};

QTEST_APPLESS_MAIN(TestFirmware)
#include "test_firmware.moc"
