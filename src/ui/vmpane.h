// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>

#include "core/argsfile.h"

class Banner;
class QPushButton;
class QTabWidget;
class QTimer;
class SettingsPage;
class Vm;
class VmDetails;

/*
 * The VM selected in the list, beside it: its details, then its settings,
 * in tabs.  The settings pages all edit a copy of the VM's arguments, which
 * keeps their changes from tab to tab until Apply saves them.
 */
class VmPane : public QWidget
{
    Q_OBJECT

public:
    enum Tab {
        Details, General, System, Display, Storage, SharedFolders, PciDevices, UsbDevices,
        Arguments,
    };

    explicit VmPane(QWidget *parent = nullptr);
    ~VmPane() override;

    Vm *vm() const;
    /* Another VM gets new pages: the changes not applied to this one are lost */
    void setVm(Vm *vm);
    VmDetails *details() const { return m_details; }

    Tab tab() const;
    void setTab(Tab tab);
    /* The settings tab shown last, for the Settings action */
    Tab settingsTab() const { return m_settingsTab; }

    /* Changes not applied yet */
    bool isModified() const;
    /*
     * Asks whether to apply the changes, if any, or discard them, with
     * @question, e.g. "Apply them before closing?": false if the user
     * cancels, or they cannot be saved
     */
    bool confirmChanges(const QString &question);
    bool apply();
    void discard();

private:
    void buildPages();
    void tabChanged(int index);
    void vmChanged();
    void updateFooter();
    void watchEdits(SettingsPage *page);

    QPointer<Vm> m_vm;
    /* The arguments the pages edit, and those they started from */
    ArgsFile m_args;
    QString m_loaded;
    QList<SettingsPage *> m_pages;
    int m_current = -1;
    Tab m_settingsTab = General;
    QTimer *m_check;
    QTabWidget *m_tabs;
    VmDetails *m_details;
    QWidget *m_footer;
    Banner *m_running;
    QPushButton *m_discard;
    QPushButton *m_apply;
};
