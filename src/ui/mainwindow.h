// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QHash>
#include <QMainWindow>

class QAction;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QSplitter;
class QStackedWidget;
class Vm;
class VmDetails;
class VmStore;

/* The VMs on the left, the selected one on the right, VirtualBox style */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(VmStore *store, QWidget *parent = nullptr);

    /* The selected VM, if any */
    Vm *current() const;
    void select(const QString &id);
    void newVm();
    void openSettings(Vm *vm, int page = -1);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void addVm(Vm *vm);
    void removeItem(const QString &id);
    QListWidgetItem *itemOf(const QString &id) const;
    void updateItem(Vm *vm);
    void updateActions();
    void updateStatus();
    void currentChanged();

    void start();
    void togglePause();
    void shutDown();
    void reset();
    void forceOff();
    void remove();
    void showLog();
    void showCommandLine();

    VmStore *m_store;
    QListWidget *m_list;
    QStackedWidget *m_right;
    VmDetails *m_details;
    QSplitter *m_splitter;
    QLabel *m_qemuStatus;
    /* Why the last start of a VM failed, by id */
    QHash<QString, QString> m_errors;

    QAction *m_new;
    QAction *m_settings;
    QAction *m_start;
    QAction *m_pause;
    QAction *m_shutDown;
    QAction *m_reset;
    QAction *m_forceOff;
    QAction *m_remove;
    QAction *m_log;
    QAction *m_folder;
    QAction *m_command;
    QAction *m_preferences;
    QAction *m_reference;
    QAction *m_quit;
};
