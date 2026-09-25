// SPDX-License-Identifier: GPL-2.0-or-later
#include "settingsdialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "core/paths.h"
#include "core/vmrunner.h"
#include "core/vmstore.h"
#include "ui/banner.h"
#include "ui/settingspages.h"

SettingsDialog::SettingsDialog(Vm *vm, QWidget *parent)
    : QDialog(parent), m_vm(vm), m_args(vm->args()), m_list(new QListWidget),
      m_stack(new QStackedWidget), m_running(new Banner(Banner::Information))
{
    auto *layout = new QVBoxLayout(this);
    auto *body = new QHBoxLayout;
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                         QDialogButtonBox::Apply);
    const QSettings settings(Paths::settingsPath(), QSettings::IniFormat);

    setWindowTitle(tr("%1 — Settings").arg(vm->name()));
    m_pages = {new GeneralPage(vm), new SystemPage, new SharesPage,
               new PciPage,         new UsbPage,    new ArgumentsPage};

    m_list->setObjectName("pages");
    m_list->setIconSize(QSize(22, 22));
    m_list->setSpacing(1);
    for (SettingsPage *page : std::as_const(m_pages)) {
        m_list->addItem(new QListWidgetItem(page->icon(), page->title()));
        m_stack->addWidget(page);
    }
    m_list->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_list->setFixedWidth(m_list->sizeHintForColumn(0) + 2 * m_list->frameWidth() + 16);

    body->addWidget(m_list);
    body->addWidget(m_stack, 1);
    layout->addWidget(m_running);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, &SettingsDialog::switchTo);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            &SettingsDialog::apply);
    connect(vm->runner(), &VmRunner::stateChanged, this, &SettingsDialog::updateRunning);
    updateRunning();

    if (!restoreGeometry(settings.value("settings/geometry").toByteArray())) {
        resize(1100, 720);
    }
    m_list->setCurrentRow(qBound(0, settings.value("settings/page").toInt(),
                                 int(m_pages.size()) - 1));
}

void SettingsDialog::setPage(Page page)
{
    m_list->setCurrentRow(page);
}

void SettingsDialog::updateRunning()
{
    if (m_vm && m_vm->runner()->isActive()) {
        m_running->setText(tr("The VM is running: the changes apply the next time it starts."));
        m_running->show();
    } else {
        m_running->hide();
    }
}

void SettingsDialog::switchTo(int row)
{
    if (row < 0 || row >= m_pages.size()) {
        return;
    }
    if (m_current >= 0) {
        m_pages[m_current]->save(m_args);
    }
    m_current = row;
    m_pages[row]->load(m_args);
    m_stack->setCurrentIndex(row);
}

bool SettingsDialog::apply()
{
    QString error;

    if (!m_vm) {
        return false;
    }
    m_pages[m_current]->save(m_args);
    if (m_args.toText() != m_vm->args().toText() && !m_vm->save(m_args, &error)) {
        QMessageBox::warning(this, tr("Cannot Save the Settings"), error);
        return false;
    }
    m_args = m_vm->args();
    m_pages[m_current]->load(m_args);
    return true;
}

void SettingsDialog::accept()
{
    if (apply()) {
        QDialog::accept();
    }
}

void SettingsDialog::done(int result)
{
    QSettings settings(Paths::settingsPath(), QSettings::IniFormat);

    settings.setValue("settings/geometry", saveGeometry());
    settings.setValue("settings/page", m_current);
    QDialog::done(result);
}
