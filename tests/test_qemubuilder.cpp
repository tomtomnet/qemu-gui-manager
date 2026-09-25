// SPDX-License-Identifier: GPL-2.0-or-later
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "core/qemubuilder.h"

/* A stand-in for QEMU: its configure writes a build.ninja making the binaries */
static const char kConfigure[] = R"(#!/bin/sh
echo "$@" >> "$(dirname "$0")/../configure-runs"
cat > build.ninja <<EOF
rule touch
  command = touch \$out
build qemu-system-x86_64: touch
build qemu-img: touch
EOF
)";

class TestQemuBuilder : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;

    static bool run(const QString &dir, const QStringList &args)
    {
        QProcess p;
        p.setWorkingDirectory(dir);
        p.start("git", QStringList{"-c", "user.name=t", "-c", "user.email=t@t"} + args);
        return p.waitForFinished(30000) && p.exitCode() == 0;
    }

    static QString build(QemuBuilder &b, const QemuBuilder::Options &o)
    {
        QSignalSpy finished(&b, &QemuBuilder::finished);
        b.start(o);
        if (!finished.wait(60000)) {
            return "timeout";
        }
        return finished[0][0].toString();
    }

    int configureRuns() const
    {
        QFile f(m_tmp.filePath("configure-runs"));
        return f.open(QIODevice::ReadOnly) ? f.readAll().count('\n') : 0;
    }

private slots:
    void initTestCase()
    {
        if (QStandardPaths::findExecutable("git").isEmpty() ||
            QStandardPaths::findExecutable("ninja").isEmpty()) {
            QSKIP("needs git and ninja");
        }
        const QString remote = m_tmp.filePath("remote");
        QVERIFY(QDir().mkpath(remote));
        QFile f(remote + "/configure");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kConfigure);
        f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeUser);
        QVERIFY(run(remote, {"init", "-q", "-b", "master"}));
        QVERIFY(run(remote, {"add", "configure"}));
        QVERIFY(run(remote, {"commit", "-q", "-m", "v1"}));
    }

    void buildAndUpdate()
    {
        const QString src = m_tmp.filePath("src");
        QemuBuilder::Options o{src, "file://" + m_tmp.filePath("remote"), "master", true,
                               QemuBuilder::defaultConfigureArgs()};
        QemuBuilder b;
        QSignalSpy progress(&b, &QemuBuilder::progress);

        /* clone, configure, compile */
        QCOMPARE(build(b, o), "");
        QVERIFY(QFileInfo(QemuBuilder::binary(src)).exists());
        QCOMPARE(configureRuns(), 1);
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.last()[0].toInt(), 2);
        QCOMPARE(progress.last()[1].toInt(), 2);

        /* fetch a new commit; the configuration is still good */
        QFile marker(m_tmp.filePath("remote/NEWS"));
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.close();
        QVERIFY(run(m_tmp.filePath("remote"), {"add", "NEWS"}));
        QVERIFY(run(m_tmp.filePath("remote"), {"commit", "-q", "-m", "v2"}));
        QCOMPARE(build(b, o), "");
        QVERIFY(QFileInfo::exists(src + "/NEWS"));
        QCOMPARE(configureRuns(), 1);

        /* other options, configured again */
        o.configureArgs << "--disable-docs";
        o.update = false;
        QCOMPARE(build(b, o), "");
        QCOMPARE(configureRuns(), 2);
    }

    void failure()
    {
        QemuBuilder b;
        const QString error = build(b, {m_tmp.filePath("nope"), m_tmp.filePath("missing"),
                                        "master", true, {}});
        QCOMPARE(error, "Downloading QEMU failed");
    }
};

QTEST_GUILESS_MAIN(TestQemuBuilder)
#include "test_qemubuilder.moc"
