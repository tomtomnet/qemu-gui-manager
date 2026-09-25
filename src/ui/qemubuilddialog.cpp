// SPDX-License-Identifier: GPL-2.0-or-later
#include "qemubuilddialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
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
#include <QStyle>
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
                               "<code>sudo dnf builddep qemu</code> (and "
                               "<code>virglrenderer</code> for a virglrenderer of its own), "
                               "and an internet connection the first time."));
    deps->setWordWrap(true);
    deps->setTextInteractionFlags(Qt::TextSelectableByMouse);
    optionsLayout->addRow(deps);
    layout->addWidget(options);

    /* virglrenderer */
    auto *virgl = new QGroupBox(tr("3D acceleration (virglrenderer)"));
    auto *virglLayout = new QFormLayout(virgl);
    auto *renderers = new QGridLayout;
    const QStringList chosen = settings.value("build/virglRenderers").toStringList();

    m_virglSystem = new QRadioButton(tr("The system's virglrenderer"));
    m_virglOwn = new QRadioButton(tr("One built by the manager, with DRM native context for:"));
    m_virglOwn->setToolTip(tr("Built into %1, which this QEMU loads instead of the "
                              "system's; rebuilding it takes effect at the next start of "
                              "a VM").arg(QemuBuilder::defaultVirglDir()));
    m_xe = new QCheckBox(tr("Intel Xe: Arc, Core Ultra and newer (patched)"));
    m_xe->setToolTip(tr("With the patch of github.com/cmspam/xe-native-context-enablement. "
                        "The guest needs the Mesa patch from there too."));
    m_i915 = new QCheckBox(tr("Intel i915: older Intel graphics"));
    m_amd = new QCheckBox(tr("AMD"));
    m_venus = new QCheckBox(tr("Vulkan through Venus"));
    m_xe->setChecked(chosen.contains("xe-experimental"));
    m_i915->setChecked(chosen.contains("i915-experimental"));
    m_amd->setChecked(chosen.contains("amdgpu-experimental"));
    m_venus->setChecked(settings.value("build/virglVenus").toBool());
    renderers->setContentsMargins(style()->pixelMetric(QStyle::PM_IndicatorWidth), 0, 0, 0);
    renderers->addWidget(m_xe, 0, 0);
    renderers->addWidget(m_i915, 0, 1);
    renderers->addWidget(m_amd, 1, 0);
    renderers->addWidget(m_venus, 1, 1);
    m_virglPatches = new QLineEdit(settings.value("build/virglPatches").toString());
    m_virglPatches->setToolTip(tr("Patch files or URLs, separated by spaces"));
    m_virglRef = new QLineEdit(settings.value("build/virglRef").toString());
    m_virglRef->setPlaceholderText(tr("main, or the newest release the patches apply to"));
    m_virglMeson = new QLineEdit(settings.value("build/virglMeson").toString());
    m_virglMeson->setPlaceholderText(tr("More meson options"));
    m_virglStatus = new QLabel;
    m_virglStatus->setWordWrap(true);
    m_virglStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    virglLayout->addRow(m_virglSystem);
    virglLayout->addRow(m_virglOwn);
    virglLayout->addRow(renderers);
    virglLayout->addRow(tr("Patches:"), m_virglPatches);
    virglLayout->addRow(tr("Version:"), m_virglRef);
    virglLayout->addRow(tr("meson:"), m_virglMeson);
    virglLayout->addRow(m_virglStatus);
    (settings.value("build/virgl").toBool() ? m_virglOwn : m_virglSystem)->setChecked(true);
    layout->addWidget(virgl);

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
    /* the Xe renderer comes with its patch */
    connect(m_xe, &QCheckBox::toggled, this, [this](bool on) {
        QStringList patches = QProcess::splitCommand(m_virglPatches->text());
        if (on && !patches.contains(QemuBuilder::xePatchUrl())) {
            patches << QemuBuilder::xePatchUrl();
        } else if (!on) {
            patches.removeAll(QemuBuilder::xePatchUrl());
        }
        m_virglPatches->setText(patches.join(' '));
    });
    connect(m_virglOwn, &QRadioButton::toggled, this, &QemuBuildDialog::updateState);
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
    updateVirglStatus();
    resize(760, 780);
}

QString QemuBuildDialog::sourceDir() const
{
    return m_managed->isChecked() ? QemuBuilder::defaultSourceDir() : m_dir->text().trimmed();
}

QStringList QemuBuildDialog::renderers() const
{
    QStringList list;

    if (m_xe->isChecked()) {
        list << "xe-experimental";
    }
    if (m_i915->isChecked()) {
        list << "i915-experimental";
    }
    if (m_amd->isChecked()) {
        list << "amdgpu-experimental";
    }
    return list;
}

void QemuBuildDialog::updateVirglStatus()
{
    const QString lib = QemuBuilder::loadedVirgl(QemuBuilder::binary(sourceDir()));

    m_virglStatus->setText(lib.isEmpty() ? QString()
                                         : tr("This QEMU loads %1").arg(lib));
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
    settings.setValue("build/virgl", m_virglOwn->isChecked());
    settings.setValue("build/virglRenderers", renderers());
    settings.setValue("build/virglVenus", m_venus->isChecked());
    settings.setValue("build/virglPatches", m_virglPatches->text().simplified());
    settings.setValue("build/virglRef", m_virglRef->text().trimmed());
    settings.setValue("build/virglMeson", m_virglMeson->text().simplified());

    options.sourceDir = sourceDir();
    options.url = QemuBuilder::defaultUrl();
    options.branch = m_branch->text().trimmed();
    options.update = managed;
    options.configureArgs = QProcess::splitCommand(m_configure->text());
    options.virgl.enabled = m_virglOwn->isChecked();
    options.virgl.dir = QemuBuilder::defaultVirglDir();
    options.virgl.url = QemuBuilder::defaultVirglUrl();
    options.virgl.ref = m_virglRef->text().trimmed();
    options.virgl.patches = QProcess::splitCommand(m_virglPatches->text());
    options.virgl.renderers = renderers();
    options.virgl.venus = m_venus->isChecked();
    options.virgl.mesonArgs = QProcess::splitCommand(m_virglMeson->text());
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
    updateVirglStatus();
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
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_virglSystem, m_virglOwn, m_xe, m_i915, m_amd, m_venus, m_virglPatches,
             m_virglRef, m_virglMeson}) {
        w->setEnabled(!running && (w == m_virglSystem || w == m_virglOwn ||
                                   m_virglOwn->isChecked()));
    }
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
