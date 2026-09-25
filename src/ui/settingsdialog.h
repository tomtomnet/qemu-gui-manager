// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDialog>
#include <QList>
#include <QPointer>

#include "core/argsfile.h"

class Banner;
class QListWidget;
class QPushButton;
class QTimer;
class QStackedWidget;
class SettingsPage;
class Vm;

/* The settings of a VM, which all edit its arguments */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    enum Page {
        General, System, Display, Storage, SharedFolders, PciDevices, UsbDevices, Arguments,
    };

    explicit SettingsDialog(Vm *vm, QWidget *parent = nullptr);

    void setPage(Page page);
    void accept() override;
    void done(int result) override;

private:
    bool apply();
    void switchTo(int row);
    void updateRunning();
    void updateApply();
    void watchEdits(SettingsPage *page);

    QPointer<Vm> m_vm;
    ArgsFile m_args;
    QList<SettingsPage *> m_pages;
    QListWidget *m_list;
    QStackedWidget *m_stack;
    Banner *m_running;
    QPushButton *m_apply;
    QTimer *m_check;
    int m_current = -1;
};
