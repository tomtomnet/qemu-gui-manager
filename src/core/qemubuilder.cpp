// SPDX-License-Identifier: GPL-2.0-or-later
#include "qemubuilder.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "core/paths.h"

/* The options the build tree was configured with, to know when to redo it */
static const char kStamp[] = "/qgm-configure-args";

/*
 * sh -c SCRIPT sh DIR REF PATCH...: checks out REF, or else the newest of
 * main and the last releases that all the patches apply to, then applies
 * them; if none takes them, main without them
 */
static const char kApplyPatches[] = R"sh(cd "$1" || exit 1
want=$2
shift 2
if [ -n "$want" ]; then
    refs=$want
else
    refs="origin/HEAD $(git tag --sort=-creatordate | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' | head -n 5)"
fi
for ref in $refs; do
    if git rev-parse -q --verify "origin/$ref" >/dev/null; then
        ref=origin/$ref
    fi
    git checkout -q -f --detach "$ref" && git clean -q -f -d -x || exit 1
    ok=1
    for p in "$@"; do
        git apply --check "$p" 2>/dev/null || { ok=0; break; }
    done
    if [ "$ok" = 1 ]; then
        for p in "$@"; do
            git apply "$p" || exit 1
        done
        echo "virglrenderer $ref, with $# patch(es)"
        exit 0
    fi
    echo "The patches do not apply to $ref"
done
[ -z "$want" ] || exit 1
echo "WARNING: the patches apply to none of $refs: building main without them"
git checkout -q -f --detach origin/HEAD && git clean -q -f -d -x
)sh";

/*
 * sh -c SCRIPT sh SRC BUILD PREFIX VENUS RENDERER... -- MESON-ARG...:
 * configures virglrenderer with the renderers and Venus it can build
 */
static const char kConfigureVirgl[] = R"sh(src=$1 build=$2 prefix=$3 venus=$4
shift 4
renderers=
while [ $# -gt 0 ] && [ "$1" != -- ]; do
    if grep -q "'$1'" "$src/meson_options.txt"; then
        renderers="$renderers${renderers:+,}$1"
    else
        echo "No $1 renderer in this virglrenderer"
    fi
    shift
done
[ $# -gt 0 ] && shift
if [ "$venus" = true ] && ! pkg-config --exists vulkan; then
    echo "WARNING: no Vulkan headers, so no Venus (sudo dnf install vulkan-loader-devel)"
    venus=false
fi
echo "Native context renderers: ${renderers:-none}; Venus: $venus"
reconfigure=
[ -f "$build/build.ninja" ] && reconfigure=--reconfigure
exec meson setup $reconfigure "$build" "$src" --prefix="$prefix" --libdir=lib \
    --buildtype=release -Ddrm-renderers="$renderers" -Dvenus="$venus" "$@"
)sh";

QString QemuBuilder::defaultSourceDir()
{
    return Paths::dataDir() + "/qemu";
}

QString QemuBuilder::defaultUrl()
{
    return "https://github.com/tomtomnet/qemu-gui.git";
}

QString QemuBuilder::defaultBranch()
{
    return "master";
}

QStringList QemuBuilder::defaultConfigureArgs()
{
    return {"--target-list=" + Paths::hostArch() + "-softmmu"};
}

QString QemuBuilder::buildDir(const QString &sourceDir)
{
    return sourceDir + "/build-qgm";
}

QString QemuBuilder::binary(const QString &sourceDir)
{
    return buildDir(sourceDir) + '/' + Paths::qemuSystemName();
}

QString QemuBuilder::defaultVirglDir()
{
    return Paths::dataDir() + "/virglrenderer";
}

QString QemuBuilder::defaultVirglUrl()
{
    return "https://gitlab.freedesktop.org/virgl/virglrenderer.git";
}

QString QemuBuilder::xePatchUrl()
{
    return "https://raw.githubusercontent.com/cmspam/xe-native-context-enablement/master/"
           "virglrenderer-xe-native-context.patch";
}

QStringList QemuBuilder::allRenderers()
{
    return {"amdgpu-experimental", "i915-experimental", "xe-experimental", "msm", "asahi",
            "panfrost-experimental"};
}

QemuBuilder::Virgl QemuBuilder::defaultVirgl()
{
    Virgl virgl;

    virgl.enabled = true;
    virgl.dir = defaultVirglDir();
    virgl.url = defaultVirglUrl();
    virgl.patches = {xePatchUrl()};
    virgl.renderers = allRenderers();
    virgl.venus = true;
    return virgl;
}

QString QemuBuilder::virglLibDir(const QString &virglDir)
{
    return virglDir + "/install/lib";
}

QString QemuBuilder::loadedVirgl(const QString &binary)
{
    static const QRegularExpression line("libvirglrenderer\\.so[.\\d]*\\s+=>\\s+(\\S+)");
    const QFileInfo fi(binary);

    /* built with modules, virtio-gpu-gl and virglrenderer come as one */
    for (const QString &file : {fi.filePath(), fi.dir().filePath("hw-display-virtio-gpu-gl.so")}) {
        QProcess ldd;

        if (!QFileInfo::exists(file)) {
            continue;
        }
        ldd.start("ldd", {file});
        if (!ldd.waitForFinished(5000)) {
            continue;
        }
        const QRegularExpressionMatch m =
            line.match(QString::fromLocal8Bit(ldd.readAllStandardOutput()));
        if (m.hasMatch()) {
            return QFileInfo(m.captured(1)).canonicalFilePath();
        }
    }
    return {};
}

int QemuBuilder::defaultJobs()
{
    static const QRegularExpression total("MemTotal:\\s+(\\d+) kB");
    QFile meminfo("/proc/meminfo");
    int memoryGiB = 4;

    if (meminfo.open(QIODevice::ReadOnly)) {
        const QRegularExpressionMatch m = total.match(QString::fromLatin1(meminfo.readAll()));
        if (m.hasMatch()) {
            memoryGiB = int(m.captured(1).toLongLong() >> 20);
        }
    }
    return qBound(1, qMin(QThread::idealThreadCount(), memoryGiB - 1), 64);
}

QemuBuilder::QemuBuilder(QObject *parent) : QObject(parent)
{
}

QemuBuilder::~QemuBuilder()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

void QemuBuilder::start(const Options &options)
{
    const QString build = buildDir(options.sourceDir);
    const int jobs = options.jobs > 0 ? options.jobs : defaultJobs();
    QFile stamp(build + kStamp);
    QString configured;
    QStringList env;

    if (isRunning()) {
        return;
    }
    m_options = options;
    m_configureArgs = options.configureArgs;
    m_steps.clear();
    m_cancelled = false;

    if (options.virgl.enabled) {
        const QString lib = virglLibDir(options.virgl.dir);
        const QString pkgConfigPath = qEnvironmentVariable("PKG_CONFIG_PATH");

        addVirglSteps(options.virgl, jobs);
        /* QEMU builds against it, and loads it from there */
        env << "PKG_CONFIG_PATH=" + lib + "/pkgconfig" +
                   (pkgConfigPath.isEmpty() ? QString() : ':' + pkgConfigPath);
        m_configureArgs << "--extra-ldflags=-Wl,-rpath," + lib;
    }

    if (!QFileInfo::exists(options.sourceDir + "/.git")) {
        m_steps << Step{tr("Downloading QEMU"), "git",
                        {"clone", "--depth", "1", "--branch", options.branch, options.url,
                         options.sourceDir}, {}};
    } else if (options.update) {
        m_steps << Step{tr("Downloading the changes"), "git",
                        {"fetch", "--depth", "1", options.url, options.branch},
                        options.sourceDir};
        m_steps << Step{tr("Updating the sources"), "git", {"reset", "--hard", "FETCH_HEAD"},
                        options.sourceDir};
    }
    if (stamp.open(QIODevice::ReadOnly)) {
        configured = QString::fromUtf8(stamp.readAll());
    }
    if (!QFileInfo::exists(build + "/build.ninja") ||
        configured != m_configureArgs.join('\n')) {
        m_steps << Step{tr("Configuring"), options.sourceDir + "/configure",
                        m_configureArgs, build, true, env};
    }
    m_steps << Step{tr("Compiling"), "ninja",
                    {"-j", QString::number(jobs), Paths::qemuSystemName(), "qemu-img"},
                    build, false, env};
    runNext();
}

void QemuBuilder::addVirglSteps(const Virgl &virgl, int jobs)
{
    const QString src = virgl.dir + "/src";
    const QString build = virgl.dir + "/build";
    const QString patchDir = virgl.dir + "/patches";
    QStringList patches, meson;

    if (!QFileInfo::exists(src + "/.git")) {
        m_steps << Step{tr("Downloading virglrenderer"), "git",
                        {"clone", "--quiet", virgl.url, src}, {}};
    } else {
        m_steps << Step{tr("Downloading the changes to virglrenderer"), "git",
                        {"fetch", "--quiet", "--tags", "--force", "origin"}, src};
    }
    for (qsizetype i = 0; i < virgl.patches.size(); i++) {
        const QString &patch = virgl.patches[i];
        const QString name = QUrl(patch).fileName();

        if (!patch.contains("://")) {
            patches << QFileInfo(patch).absoluteFilePath();
            continue;
        }
        patches << QString("%1/%2-%3").arg(patchDir).arg(i + 1)
                       .arg(name.isEmpty() ? QString("patch") : name);
        m_steps << Step{tr("Downloading %1").arg(name), "curl",
                        {"--fail", "--silent", "--show-error", "--location", "--retry", "2",
                         "--output", patches.last(), patch}, patchDir};
    }
    m_steps << Step{tr("Patching virglrenderer"), "sh",
                    QStringList{"-c", kApplyPatches, "sh", src, virgl.ref} + patches, src};

    meson << "-c" << kConfigureVirgl << "sh" << src << build << virgl.dir + "/install"
          << (virgl.venus ? "true" : "false") << virgl.renderers << "--" << virgl.mesonArgs;
    m_steps << Step{tr("Configuring virglrenderer"), "sh", meson, virgl.dir};
    m_steps << Step{tr("Compiling virglrenderer"), "ninja",
                    {"-C", build, "-j", QString::number(jobs), "install"}, virgl.dir};
}

void QemuBuilder::cancel()
{
    if (!m_process) {
        return;
    }
    m_cancelled = true;
    m_process->terminate();
    QTimer::singleShot(5000, m_process, &QProcess::kill);
}

void QemuBuilder::runNext()
{
    if (m_steps.isEmpty()) {
        emit finished({});
        return;
    }

    const Step step = m_steps.first();
    if (!step.dir.isEmpty()) {
        QDir().mkpath(step.dir);
    }
    emit stepStarted(step.description);
    emit output(QString("$ %1 %2\n").arg(step.program, step.args.join(' ')));

    m_line.clear();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setWorkingDirectory(step.dir);
    if (!step.env.isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QString &var : step.env) {
            env.insert(var.section('=', 0, 0), var.section('=', 1));
        }
        m_process->setProcessEnvironment(env);
    }
    connect(m_process, &QProcess::readyRead, this, [this]() {
        const QString text = QString::fromLocal8Bit(m_process->readAll());
        emit output(text);
        parseProgress(text);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this, step](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            const QString error = m_process->errorString();
            m_process->deleteLater();
            m_process = nullptr;
            emit finished(tr("Cannot run %1: %2").arg(step.program, error));
        }
    });
    connect(m_process, &QProcess::finished, this,
            [this, step](int code, QProcess::ExitStatus status) {
        m_process->deleteLater();
        m_process = nullptr;
        if (m_cancelled) {
            emit finished(tr("Cancelled"));
            return;
        }
        if (status != QProcess::NormalExit || code != 0) {
            emit finished(tr("%1 failed").arg(step.description));
            return;
        }
        if (step.configure) {
            QFile stamp(buildDir(m_options.sourceDir) + kStamp);
            if (stamp.open(QIODevice::WriteOnly)) {
                stamp.write(m_configureArgs.join('\n').toUtf8());
            }
        }
        m_steps.removeFirst();
        runNext();
    });
    m_process->start(step.program, step.args);
}

void QemuBuilder::parseProgress(const QString &text)
{
    static const QRegularExpression status("^\\[(\\d+)/(\\d+)\\]");

    m_line += text;
    qsizetype end;
    while ((end = m_line.indexOf('\n')) >= 0) {
        const QRegularExpressionMatch m = status.match(m_line.left(end));
        if (m.hasMatch()) {
            emit progress(m.captured(1).toInt(), m.captured(2).toInt());
        }
        m_line.remove(0, end + 1);
    }
}
