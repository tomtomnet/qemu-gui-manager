// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDialog>
#include <QIcon>
#include <QList>
#include <QWidget>

#include "core/argsfile.h"
#include "core/vmconfig.h"

class ArgsEditorPane;
class Banner;
class QemuDocs;
class QRadioButton;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class Vm;

/*
 * A page of the settings dialog.  All pages edit the same arguments: a
 * page shows them when it is entered and writes back what the user changed
 * when it is left, so that the other pages, the Arguments page above all,
 * see the changes.
 */
class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    using QWidget::QWidget;

    virtual QString title() const = 0;
    virtual QIcon icon() const = 0;
    virtual void load(const ArgsFile &args) = 0;
    /* Only the settings the user changed, to keep the rest as written */
    virtual void save(ArgsFile &args) = 0;
    /* The user changed something since load() */
    virtual bool isModified() const = 0;
};

class GeneralPage : public SettingsPage
{
    Q_OBJECT

public:
    explicit GeneralPage(Vm *vm, QWidget *parent = nullptr);

    QString title() const override { return tr("General"); }
    QIcon icon() const override;
    void load(const ArgsFile &args) override;
    void save(ArgsFile &args) override;
    bool isModified() const override;

private:
    QLineEdit *m_name;
    QString m_loaded;
};

class SystemPage : public SettingsPage
{
    Q_OBJECT

public:
    explicit SystemPage(QWidget *parent = nullptr);

    QString title() const override { return tr("System"); }
    QIcon icon() const override;
    void load(const ArgsFile &args) override;
    void save(ArgsFile &args) override;
    bool isModified() const override;

private:
    /* The QEMU chosen, empty for the default one */
    QString chosenQemu() const;
    void updateQemu();
    void fillLists();
    void updateTopology();
    void describe();

    QemuDocs *m_docs = nullptr;
    QSlider *m_memorySlider;
    QSpinBox *m_memory;
    QSlider *m_cpuSlider;
    QSpinBox *m_cpus;
    QCheckBox *m_topology;
    QSpinBox *m_sockets;
    QSpinBox *m_cores;
    QSpinBox *m_threads;
    QComboBox *m_model;
    QLabel *m_modelInfo;
    QComboBox *m_machine;
    QLabel *m_machineInfo;
    QComboBox *m_accel;
    QRadioButton *m_defaultQemu;
    QRadioButton *m_ownQemu;
    QLineEdit *m_qemuPath;
    QLabel *m_qemuInfo;

    qint64 m_loadedMemory = 0;
    VmConfig::Cpus m_loadedCpus;
    QString m_loadedMachine;
    QString m_loadedAccel;
    QString m_loadedQemu;
};

class SharesPage : public SettingsPage
{
    Q_OBJECT

public:
    explicit SharesPage(QWidget *parent = nullptr);

    QString title() const override { return tr("Shared Folders"); }
    QIcon icon() const override;
    void load(const ArgsFile &args) override;
    void save(ArgsFile &args) override;
    bool isModified() const override;

private:
    void fill();
    void edit(int row);
    void updateHints();

    Banner *m_virtiofsd;
    Banner *m_memory;
    QTableWidget *m_table;
    QPushButton *m_edit;
    QPushButton *m_remove;
    QLabel *m_mount;

    QList<VmConfig::Share> m_shares;
    QList<VmConfig::Share> m_loaded;
    bool m_sharedMemory = false;
    bool m_fixMemory = false;
};

/* Adds or edits a shared folder */
class ShareDialog : public QDialog
{
    Q_OBJECT

public:
    ShareDialog(const VmConfig::Share &share, const QStringList &otherTags,
                QWidget *parent = nullptr);

    VmConfig::Share share() const;

private:
    void validate();

    QLineEdit *m_path;
    QLineEdit *m_tag;
    QComboBox *m_cache;
    QLabel *m_cacheInfo;
    QCheckBox *m_readonly;
    QLabel *m_error;
    QPushButton *m_ok;
    QStringList m_otherTags;
    bool m_tagEdited = false;
};

class PciPage : public SettingsPage
{
    Q_OBJECT

public:
    explicit PciPage(QWidget *parent = nullptr);

    QString title() const override { return tr("PCI Devices"); }
    QIcon icon() const override;
    void load(const ArgsFile &args) override;
    void save(ArgsFile &args) override;
    bool isModified() const override;

private:
    QStringList checked() const;

    Banner *m_banner;
    QTreeWidget *m_tree;
    QStringList m_loaded;
};

class UsbPage : public SettingsPage
{
    Q_OBJECT

public:
    explicit UsbPage(QWidget *parent = nullptr);

    QString title() const override { return tr("USB Devices"); }
    QIcon icon() const override;
    void load(const ArgsFile &args) override;
    void save(ArgsFile &args) override;
    bool isModified() const override;

private:
    QList<VmConfig::UsbId> checked() const;

    Banner *m_controller;
    QTreeWidget *m_tree;
    QList<VmConfig::UsbId> m_loaded;
    bool m_addController = false;
};

class ArgumentsPage : public SettingsPage
{
    Q_OBJECT

public:
    explicit ArgumentsPage(QWidget *parent = nullptr);

    QString title() const override { return tr("Arguments"); }
    QIcon icon() const override;
    void load(const ArgsFile &args) override;
    void save(ArgsFile &args) override;
    bool isModified() const override;

private:
    ArgsEditorPane *m_pane;
    QString m_loaded;
};
