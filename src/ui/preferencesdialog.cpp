// SPDX-License-Identifier: GPL-2.0-or-later
#include "preferencesdialog.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "core/paths.h"
#include "ui/icons.h"
#include "ui/qemudocs.h"
#include "ui/widgets.h"

static QString autoQemu()
{
    return QStandardPaths::findExecutable("qemu-system-x86_64");
}

static QString autoVirtiofsd()
{
    if (QFileInfo("/usr/libexec/virtiofsd").isExecutable()) {
        return "/usr/libexec/virtiofsd";
    }
    return QStandardPaths::findExecutable("virtiofsd");
}

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent), m_qemu(new QLineEdit), m_qemuStatus(Widgets::hint()),
      m_virtiofsd(new QLineEdit), m_virtiofsdStatus(Widgets::hint()), m_timer(new QTimer(this))
{
    auto *layout = new QVBoxLayout(this);
    auto *form = Widgets::form();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *vms = new QLabel(QString("<a href=\"%1\">%2</a>")
                               .arg(QUrl::fromLocalFile(Paths::vmsDir()).toString(),
                                    QDir::toNativeSeparators(Paths::vmsDir()).toHtmlEscaped()));
    const QString qemu = Paths::qemuBinary();
    const QString virtiofsd = Paths::virtiofsd();

    setWindowTitle(tr("Preferences"));
    m_qemu->setObjectName("qemu");
    m_virtiofsd->setObjectName("virtiofsd");
    m_qemu->setPlaceholderText(autoQemu().isEmpty() ? tr("qemu-system-x86_64 from PATH")
                                                    : autoQemu());
    m_virtiofsd->setPlaceholderText(autoVirtiofsd().isEmpty() ? tr("virtiofsd from PATH")
                                                              : autoVirtiofsd());
    /* empty when automatic */
    m_qemu->setText(qemu == autoQemu() ? QString() : qemu);
    m_virtiofsd->setText(virtiofsd == autoVirtiofsd() ? QString() : virtiofsd);
    vms->setOpenExternalLinks(true);

    form->addRow(Widgets::label(tr("&QEMU:"), m_qemu),
                 Widgets::browseRow(m_qemu, tr("QEMU Binary")));
    form->addRow(QString(), m_qemuStatus);
    form->addRow(Widgets::label(tr("&virtiofsd:"), m_virtiofsd),
                 Widgets::browseRow(m_virtiofsd, tr("virtiofsd Binary")));
    form->addRow(QString(), m_virtiofsdStatus);
    form->addRow(tr("Virtual machines:"), vms);
    layout->addLayout(form);
    layout->addStretch();
    layout->addWidget(buttons);

    m_timer->setSingleShot(true);
    m_timer->setInterval(400);
    connect(m_timer, &QTimer::timeout, this, &PreferencesDialog::checkQemu);
    connect(m_qemu, &QLineEdit::textChanged, m_timer, qOverload<>(&QTimer::start));
    connect(m_virtiofsd, &QLineEdit::textChanged, this, &PreferencesDialog::checkVirtiofsd);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    checkQemu();
    checkVirtiofsd();
    resize(640, sizeHint().height());
}

void PreferencesDialog::checkQemu()
{
    const QString binary = m_qemu->text().trimmed().isEmpty() ? autoQemu()
                                                               : m_qemu->text().trimmed();

    if (m_version) {
        m_version->disconnect(this);
        m_version->kill();
        m_version->deleteLater();
        m_version = nullptr;
    }
    if (binary.isEmpty()) {
        m_qemuStatus->setText(tr("qemu-system-x86_64 is not in PATH: choose the QEMU to "
                                 "use, for example the one you built."));
        return;
    }
    if (!QFileInfo(binary).isExecutable()) {
        m_qemuStatus->setText(tr("This file is not an executable."));
        return;
    }

    m_qemuStatus->setText(tr("Checking…"));
    m_version = new QProcess(this);
    m_version->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_version, &QProcess::finished, this, [this]() {
        const QString first = QString::fromLocal8Bit(m_version->readAll()).section('\n', 0, 0);
        m_qemuStatus->setText(first.contains("version") ? first.trimmed()
                                                        : tr("This does not look like QEMU."));
        m_version->deleteLater();
        m_version = nullptr;
    });
    connect(m_version, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_qemuStatus->setText(tr("Cannot run it: %1").arg(m_version->errorString()));
            m_version->deleteLater();
            m_version = nullptr;
        }
    });
    m_version->start(binary, {"-version"});
}

void PreferencesDialog::checkVirtiofsd()
{
    const QString binary = m_virtiofsd->text().trimmed().isEmpty()
                               ? autoVirtiofsd()
                               : m_virtiofsd->text().trimmed();

    if (binary.isEmpty()) {
        m_virtiofsdStatus->setText(
            tr("Not found: shared folders need it. Install it with "
               "<code>sudo dnf install virtiofsd</code>."));
    } else if (!QFileInfo(binary).isExecutable()) {
        m_virtiofsdStatus->setText(tr("This file is not an executable."));
    } else {
        m_virtiofsdStatus->setText(tr("Shared folders use %1.").arg(binary.toHtmlEscaped()));
    }
}

void PreferencesDialog::accept()
{
    Paths::setQemuBinary(m_qemu->text().trimmed());
    Paths::setVirtiofsd(m_virtiofsd->text().trimmed());
    QemuDocs::reloadPreferred();
    QDialog::accept();
}
