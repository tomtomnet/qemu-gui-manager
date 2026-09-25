// SPDX-License-Identifier: GPL-2.0-or-later
#include <QFile>
#include <QFileInfo>
#include <QDir>
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

/* A QEMU whose configure records what it finds of virglrenderer */
static const char kVirglConfigure[] = R"sh(#!/bin/sh
echo "$@" > configure-args
pkg-config --variable=libdir virglrenderer > virgl-libdir
cat > build.ninja <<EOF
rule touch
  command = touch \$out
build qemu-system-x86_64: touch
build qemu-img: touch
EOF
)sh";

/* A stand-in for virglrenderer: a library, and the renderers it accepts */
static const char kVirglMeson[] = R"meson(project('virglrenderer', 'c', version : '1.3.0')
lib = shared_library('virglrenderer', 'virgl.c', version : '1.9.0', install : true)
import('pkgconfig').generate(lib, name : 'virglrenderer', description : 'test')
)meson";
static const char kVirglOptions[] = R"meson(option('drm-renderers', type : 'array', value : [],
       choices : ['amdgpu-experimental'])
option('venus', type : 'boolean', value : false)
)meson";

class TestQemuBuilder : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;

    static bool write(const QString &path, const QByteArray &data, bool executable = false)
    {
        QFile f(path);
        if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !f.open(QIODevice::WriteOnly) ||
            f.write(data) != data.size()) {
            return false;
        }
        f.close();
        return !executable || f.setPermissions(f.permissions() | QFileDevice::ExeUser);
    }

    static QString read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
    }

    static QString git(const QString &dir, const QStringList &args)
    {
        QProcess p;
        p.setWorkingDirectory(dir);
        p.start("git", QStringList{"-c", "user.name=t", "-c", "user.email=t@t"} + args);
        p.waitForFinished(30000);
        return QString::fromUtf8(p.readAllStandardOutput());
    }

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

    /* A patched virglrenderer of our own, which QEMU builds against */
    void virgl()
    {
        for (const char *tool : {"meson", "cc", "pkg-config", "curl"}) {
            if (QStandardPaths::findExecutable(tool).isEmpty()) {
                QSKIP("needs meson, cc, pkg-config and curl");
            }
        }
        const QString virglRemote = m_tmp.filePath("virgl-remote");
        const QString qemuRemote = m_tmp.filePath("qemu-remote");
        const QString patch = m_tmp.filePath("xe.patch");

        /* 1.3.0 takes the patch, main has changed the same line since */
        QVERIFY(write(virglRemote + "/meson.build", kVirglMeson));
        QVERIFY(write(virglRemote + "/meson_options.txt", kVirglOptions));
        QVERIFY(write(virglRemote + "/virgl.c", "int virgl_renderer_init(void) { return 0; }\n"));
        git(virglRemote, {"init", "-q", "-b", "main"});
        git(virglRemote, {"add", "."});
        git(virglRemote, {"commit", "-q", "-m", "1.3.0"});
        git(virglRemote, {"tag", "1.3.0"});
        QVERIFY(write(virglRemote + "/meson_options.txt",
                      QByteArray(kVirglOptions).replace("'amdgpu-experimental'",
                                                        "'amdgpu-experimental', 'xe-experimental'")));
        QVERIFY(write(patch, git(virglRemote, {"diff"}).toUtf8()));
        QVERIFY(write(virglRemote + "/meson_options.txt",
                      QByteArray(kVirglOptions).replace("'amdgpu-experimental'",
                                                        "'amdgpu-experimental', 'msm'")));
        git(virglRemote, {"commit", "-q", "-a", "-m", "msm"});

        QVERIFY(write(qemuRemote + "/configure", kVirglConfigure, true));
        git(qemuRemote, {"init", "-q", "-b", "master"});
        git(qemuRemote, {"add", "configure"});
        git(qemuRemote, {"commit", "-q", "-m", "qemu"});

        const QString src = m_tmp.filePath("qemu-src");
        QemuBuilder::Options o{src, "file://" + qemuRemote, "master", true,
                               QemuBuilder::defaultConfigureArgs(), {}, 2};
        o.virgl.enabled = true;
        o.virgl.dir = m_tmp.filePath("virgl");
        o.virgl.url = "file://" + virglRemote;
        o.virgl.patches = {"file://" + patch};
        o.virgl.renderers = {"xe-experimental"};

        QemuBuilder b;
        QSignalSpy output(&b, &QemuBuilder::output);
        const QString error = build(b, o);
        QString log;
        for (const QList<QVariant> &args : std::as_const(output)) {
            log += args[0].toString();
        }
        QVERIFY2(error.isEmpty(), qPrintable(error + "\n" + log));
        QVERIFY2(log.contains("The patches do not apply to origin/HEAD"), qPrintable(log));
        QVERIFY2(log.contains("virglrenderer 1.3.0, with 1 patch(es)"), qPrintable(log));

        const QString lib = QemuBuilder::virglLibDir(o.virgl.dir);
        QVERIFY(QFileInfo::exists(lib + "/libvirglrenderer.so.1"));
        QCOMPARE(read(QemuBuilder::buildDir(src) + "/virgl-libdir"), lib);
        QVERIFY(read(QemuBuilder::buildDir(src) + "/configure-args")
                    .endsWith("--extra-ldflags=-Wl,-rpath," + lib));
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
