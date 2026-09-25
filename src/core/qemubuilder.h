// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>
#include <QStringList>

class QProcess;

/*
 * Builds QEMU from a git checkout, by default a clone of the qemu-gui fork
 * the manager keeps in its data folder: fetches the branch, configures the
 * build tree (again only when the options change) and compiles just the
 * emulator and qemu-img.  QEMU then runs from the build tree, which also
 * gives the reference the full documentation.
 */
class QemuBuilder : public QObject
{
    Q_OBJECT

public:
    struct Options {
        QString sourceDir;          // cloned from url if it doesn't exist
        QString url;
        QString branch;
        /* Fetch the branch first, discarding changes to the checkout */
        bool update = true;
        QStringList configureArgs;
    };

    static QString defaultSourceDir();
    static QString defaultUrl();
    static QString defaultBranch();
    static QStringList defaultConfigureArgs();
    /* build-qgm in @sourceDir */
    static QString buildDir(const QString &sourceDir);
    static QString binary(const QString &sourceDir);

    explicit QemuBuilder(QObject *parent = nullptr);
    ~QemuBuilder() override;

    bool isRunning() const { return m_process != nullptr; }
    void start(const Options &options);
    void cancel();

signals:
    /* e.g. "Configuring" */
    void stepStarted(const QString &description);
    /* The output of the tools, as it comes */
    void output(const QString &text);
    /* From ninja's [done/total] */
    void progress(int done, int total);
    /* @error is empty on success */
    void finished(const QString &error);

private:
    struct Step {
        QString description;
        QString program;
        QStringList args;
        QString dir;
        bool configure = false;
    };

    void runNext();
    void parseProgress(const QString &text);

    QList<Step> m_steps;
    Options m_options;
    QProcess *m_process = nullptr;
    QString m_line;
    bool m_cancelled = false;
};
