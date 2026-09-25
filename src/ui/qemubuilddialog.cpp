// SPDX-License-Identifier: GPL-2.0-or-later
#include "qemubuilddialog.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QVBoxLayout>

#include "core/paths.h"
#include "core/qemubuilder.h"
#include "ui/widgets.h"

/* The build options of the fork's README */
static const char kLean[] =
    "--target-list=x86_64-softmmu --without-default-features "
    "--enable-kvm --enable-tcg --enable-pixman --enable-attr --enable-virtfs "
    "--enable-hmp --enable-malloc-trim "
    "--enable-sdl --enable-sdl-gui --enable-gtk --enable-opengl --enable-virglrenderer "
    "--enable-libusb --enable-pa --enable-pipewire --enable-spice-protocol "
    "--enable-passt --enable-gio --enable-slirp "
    "--enable-tpm --enable-vhost-kernel --enable-vhost-net --enable-vhost-user "
    "--enable-zstd --enable-png --enable-tools --enable-fdt=internal --disable-docs";

QemuBuildDialog::QemuBuildDialog(QWidget *parent)
    : QDialog(parent), m_builder(new QemuBuilder(this))
{
    QSettings settings(Paths::settingsPath(), QSettings::IniFormat);
    auto *layout = new QVBoxLayout(this);

    setWindowTitle(tr("Build QEMU"));

    /* sources */
    auto *sources = new QGroupBox(tr("Sources"));
    auto *sourcesLayout = new QFormLayout(sources);
    auto *ownRow = new QHBoxLayout;
    auto *browse = new QPushButton(tr("Browse…"));

    m_managed = new QRadioButton(tr("qemu-gui, downloaded and kept up to date by the manager"));
    m_managed->setToolTip(tr("From %1, into %2")
                              .arg(QemuBuilder::defaultUrl(), QemuBuilder::defaultSourceDir()));
    m_branch = new QLineEdit(settings.value("build/branch", QemuBuilder::defaultBranch()).toString());
    m_own = new QRadioButton(tr("My own checkout, built as it is:"));
    m_dir = new QLineEdit(settings.value("build/dir").toString());
    m_dir->setPlaceholderText(tr("A QEMU source folder"));
    ownRow->addWidget(m_dir);
    ownRow->addWidget(browse);
    sourcesLayout->addRow(m_managed);
    sourcesLayout->addRow(tr("Branch:"), m_branch);
    sourcesLayout->addRow(m_own);
    sourcesLayout->addRow(ownRow);
    (settings.value("build/own").toBool() ? m_own : m_managed)->setChecked(true);
    layout->addWidget(sources);

    /* options */
    auto *options = new QGroupBox(tr("Options"));
    auto *optionsLayout = new QFormLayout(options);

    m_preset = new QComboBox;
    m_preset->addItem(tr("Everything QEMU finds, for x86-64 only"),
                      QemuBuilder::defaultConfigureArgs().join(' '));
    m_preset->addItem(tr("Only what a desktop VM uses (faster)"), QString(kLean));
    m_preset->addItem(tr("Custom"));
    m_configure = new QLineEdit(settings.value("build/configure",
                                               QemuBuilder::defaultConfigureArgs().join(' '))
                                    .toString());
    m_configure->setToolTip(tr("The options for QEMU's configure script"));
    optionsLayout->addRow(tr("Build:"), m_preset);
    optionsLayout->addRow(tr("configure:"), m_configure);
    auto *deps = new QLabel(tr("Building needs QEMU's build dependencies, on Fedora: "
                               "<code>sudo dnf builddep qemu</code>, and an internet "
                               "connection the first time."));
    deps->setWordWrap(true);
    deps->setTextInteractionFlags(Qt::TextSelectableByMouse);
    optionsLayout->addRow(deps);
    layout->addWidget(options);

    /* progress */
    m_step = new QLabel;
    m_progress = new QProgressBar;
    m_progress->setVisible(false);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(m_step);
    layout->addWidget(m_progress);
    layout->addWidget(m_log, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    m_build = buttons->addButton(tr("Update and Build"), QDialogButtonBox::ActionRole);
    m_cancel = buttons->addButton(tr("Stop"), QDialogButtonBox::ActionRole);
    m_use = buttons->addButton(tr("Use for the VMs"), QDialogButtonBox::ActionRole);
    m_build->setDefault(true);
    layout->addWidget(buttons);

    auto syncPreset = [this]() {
        const int i = m_preset->findData(m_configure->text().simplified());
        m_preset->blockSignals(true);
        m_preset->setCurrentIndex(i < 0 ? m_preset->count() - 1 : i);
        m_preset->blockSignals(false);
    };
    syncPreset();
    connect(m_configure, &QLineEdit::textEdited, this, syncPreset);
    connect(m_preset, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!m_preset->itemData(i).isNull()) {
            m_configure->setText(m_preset->itemData(i).toString());
        }
    });
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("QEMU Sources"),
                                                               m_dir->text());
        if (!dir.isEmpty()) {
            m_dir->setText(dir);
            m_own->setChecked(true);
        }
    });
    connect(m_managed, &QRadioButton::toggled, this, &QemuBuildDialog::updateState);
    connect(m_dir, &QLineEdit::textChanged, this, &QemuBuildDialog::updateState);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(m_build, &QPushButton::clicked, this, &QemuBuildDialog::build);
    connect(m_cancel, &QPushButton::clicked, m_builder, &QemuBuilder::cancel);
    connect(m_use, &QPushButton::clicked, this, [this]() {
        const QString binary = QemuBuilder::binary(sourceDir());
        Paths::setQemuBinary(binary);
        emit qemuChanged(binary);
        m_step->setText(tr("The VMs now use %1").arg(binary));
    });

    connect(m_builder, &QemuBuilder::stepStarted, m_step, &QLabel::setText);
    connect(m_builder, &QemuBuilder::output, this, [this](const QString &text) {
        m_log->moveCursor(QTextCursor::End);
        m_log->insertPlainText(QString(text).replace('\r', '\n'));
        m_log->moveCursor(QTextCursor::End);
    });
    connect(m_builder, &QemuBuilder::progress, this, [this](int done, int total) {
        m_progress->setMaximum(total);
        m_progress->setValue(done);
    });
    connect(m_builder, &QemuBuilder::finished, this, &QemuBuildDialog::finished);

    updateState();
    resize(760, 620);
}

QString QemuBuildDialog::sourceDir() const
{
    return m_managed->isChecked() ? QemuBuilder::defaultSourceDir() : m_dir->text().trimmed();
}

void QemuBuildDialog::build()
{
    QSettings settings(Paths::settingsPath(), QSettings::IniFormat);
    const bool managed = m_managed->isChecked();
    QemuBuilder::Options options;

    settings.setValue("build/own", !managed);
    settings.setValue("build/dir", m_dir->text().trimmed());
    settings.setValue("build/branch", m_branch->text().trimmed());
    settings.setValue("build/configure", m_configure->text().simplified());

    options.sourceDir = sourceDir();
    options.url = QemuBuilder::defaultUrl();
    options.branch = m_branch->text().trimmed();
    options.update = managed;
    options.configureArgs = QProcess::splitCommand(m_configure->text());
    if (!managed && !QFileInfo::exists(options.sourceDir + "/configure")) {
        QMessageBox::warning(this, windowTitle(),
                             tr("%1 has no configure script: pick the top folder of a QEMU "
                                "checkout.").arg(options.sourceDir));
        return;
    }

    m_log->clear();
    m_progress->setValue(0);
    m_progress->setMaximum(0);
    m_progress->setVisible(true);
    m_builder->start(options);
    updateState();
}

void QemuBuildDialog::finished(const QString &error)
{
    m_progress->setVisible(false);
    if (error.isEmpty()) {
        QProcess version;
        version.start(QemuBuilder::binary(sourceDir()), {"--version"});
        version.waitForFinished(5000);
        m_step->setText(tr("Built: %1")
                            .arg(QString::fromLocal8Bit(version.readAll()).section('\n', 0, 0)));
    } else {
        m_step->setText(error + tr(", see the log below."));
    }
    updateState();
}

void QemuBuildDialog::updateState()
{
    const bool running = m_builder->isRunning();
    const QString binary = QemuBuilder::binary(sourceDir());

    m_branch->setEnabled(!running && m_managed->isChecked());
    m_dir->setEnabled(!running && m_own->isChecked());
    m_build->setEnabled(!running);
    m_build->setText(m_managed->isChecked() ? tr("Update and Build") : tr("Build"));
    m_cancel->setVisible(running);
    m_use->setEnabled(!running && QFileInfo(binary).isExecutable() &&
                      Paths::qemuBinary() != binary);
}

void QemuBuildDialog::closeEvent(QCloseEvent *event)
{
    if (m_builder->isRunning()) {
        if (!Widgets::confirm(this, QMessageBox::Question, tr("Stop the build?"),
                              tr("The build stops where it is; building again goes on "
                                 "from there."),
                              tr("&Stop"))) {
            event->ignore();
            return;
        }
        m_builder->cancel();
    }
    event->accept();
}
