// SPDX-License-Identifier: GPL-2.0-or-later
#include "qemubuilder.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

#include "core/paths.h"

/* The options the build tree was configured with, to know when to redo it */
static const char kStamp[] = "/qgm-configure-args";

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
    return {"--target-list=x86_64-softmmu"};
}

QString QemuBuilder::buildDir(const QString &sourceDir)
{
    return sourceDir + "/build-qgm";
}

QString QemuBuilder::binary(const QString &sourceDir)
{
    return buildDir(sourceDir) + "/qemu-system-x86_64";
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
    QFile stamp(build + kStamp);
    QString configured;

    if (isRunning()) {
        return;
    }
    m_options = options;
    m_steps.clear();
    m_cancelled = false;

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
        configured != options.configureArgs.join('\n')) {
        m_steps << Step{tr("Configuring"), options.sourceDir + "/configure",
                        options.configureArgs, build, true};
    }
    m_steps << Step{tr("Compiling"), "ninja", {"qemu-system-x86_64", "qemu-img"}, build};
    runNext();
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
                stamp.write(m_options.configureArgs.join('\n').toUtf8());
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
