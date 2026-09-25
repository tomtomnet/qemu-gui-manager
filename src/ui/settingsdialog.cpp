// SPDX-License-Identifier: GPL-2.0-or-later
#include "settingsdialog.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/paths.h"
#include "core/vmrunner.h"
#include "core/vmstore.h"
#include "ui/banner.h"
#include "ui/settingspages.h"

SettingsDialog::SettingsDialog(Vm *vm, QWidget *parent)
    : QDialog(parent), m_vm(vm), m_args(vm->args()), m_list(new QListWidget),
      m_stack(new QStackedWidget), m_running(new Banner(Banner::Information)),
      m_check(new QTimer(this))
{
    auto *layout = new QVBoxLayout(this);
    auto *body = new QHBoxLayout;
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                         QDialogButtonBox::Apply);
    const QSettings settings(Paths::settingsPath(), QSettings::IniFormat);

    setWindowTitle(tr("%1 — Settings").arg(vm->name()));
    m_pages = {new GeneralPage(vm), new SystemPage, new SharesPage,
               new PciPage,         new UsbPage,    new ArgumentsPage(vm->dir())};

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
    m_apply = buttons->button(QDialogButtonBox::Apply);
    connect(m_apply, &QPushButton::clicked, this, &SettingsDialog::apply);
    /* Apply is for when there is something to apply */
    m_check->setSingleShot(true);
    m_check->setInterval(0);
    connect(m_check, &QTimer::timeout, this, &SettingsDialog::updateApply);
    for (SettingsPage *page : std::as_const(m_pages)) {
        watchEdits(page);
    }
    connect(vm, &Vm::changed, m_check, qOverload<>(&QTimer::start));
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

void SettingsDialog::updateApply()
{
    m_apply->setEnabled(m_vm && (m_args.toText() != m_vm->args().toText() ||
                                 (m_current >= 0 && m_pages[m_current]->isModified())));
}

/*
 * Checks again for something to apply after any change on @page.  The
 * connections go to the timer, which is deleted before the pages, whose
 * models still signal while they are destroyed.
 */
void SettingsDialog::watchEdits(SettingsPage *page)
{
    QTimer *timer = m_check;
    const auto check = qOverload<>(&QTimer::start);

    for (QLineEdit *w : page->findChildren<QLineEdit *>()) {
        connect(w, &QLineEdit::textChanged, timer, check);
    }
    for (QSpinBox *w : page->findChildren<QSpinBox *>()) {
        connect(w, &QSpinBox::valueChanged, timer, check);
    }
    for (QComboBox *w : page->findChildren<QComboBox *>()) {
        connect(w, &QComboBox::currentIndexChanged, timer, check);
    }
    /* the pages' own handlers of a click, run first, may open dialogs */
    for (QAbstractButton *w : page->findChildren<QAbstractButton *>()) {
        connect(w, &QAbstractButton::toggled, timer, check);
        connect(w, &QAbstractButton::clicked, timer, check);
    }
    for (QPlainTextEdit *w : page->findChildren<QPlainTextEdit *>()) {
        connect(w, &QPlainTextEdit::textChanged, timer, check);
    }
    for (QAbstractItemView *w : page->findChildren<QAbstractItemView *>()) {
        if (QAbstractItemModel *model = w->model()) {
            connect(model, &QAbstractItemModel::dataChanged, timer, check);
            connect(model, &QAbstractItemModel::rowsInserted, timer, check);
            connect(model, &QAbstractItemModel::rowsRemoved, timer, check);
            connect(model, &QAbstractItemModel::modelReset, timer, check);
        }
    }
}

void SettingsDialog::switchTo(int row)
{
    if (row < 0 || row >= m_pages.size()) {
        return;
    }
    if (m_current >= 0 && m_pages[m_current]->isModified()) {
        m_pages[m_current]->save(m_args);
    }
    m_current = row;
    m_pages[row]->load(m_args);
    m_stack->setCurrentIndex(row);
    updateApply();
}

bool SettingsDialog::apply()
{
    QString error;

    if (!m_vm) {
        return false;
    }
    if (m_pages[m_current]->isModified()) {
        m_pages[m_current]->save(m_args);
    }
    if (m_args.toText() != m_vm->args().toText() && !m_vm->save(m_args, &error)) {
        QMessageBox::warning(this, tr("Cannot Save the Settings"), error);
        return false;
    }
    m_args = m_vm->args();
    m_pages[m_current]->load(m_args);
    updateApply();
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
