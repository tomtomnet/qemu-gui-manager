// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QemuBuilder;

/*
 * Builds QEMU, by default the qemu-gui fork, and makes it the QEMU of the
 * VMs: pick the sources and the configure options, then Update and Build.
 */
class QemuBuildDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QemuBuildDialog(QWidget *parent = nullptr);

signals:
    /* The binary chosen as the QEMU of the VMs */
    void qemuChanged(const QString &binary);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QString sourceDir() const;
    void build();
    void finished(const QString &error);
    void updateState();

    QemuBuilder *m_builder;
    QRadioButton *m_managed;
    QRadioButton *m_own;
    QLineEdit *m_branch;
    QLineEdit *m_dir;
    QComboBox *m_preset;
    QLineEdit *m_configure;
    QLabel *m_step;
    QProgressBar *m_progress;
    QPlainTextEdit *m_log;
    QPushButton *m_build;
    QPushButton *m_cancel;
    QPushButton *m_use;
};
