// SPDX-License-Identifier: GPL-2.0-or-later
#include "settingspages.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

#include "core/hostdevices.h"
#include "core/paths.h"
#include "core/qemuinfo.h"
#include "core/vmstore.h"
#include "ui/argseditor.h"
#include "ui/banner.h"
#include "ui/icons.h"
#include "ui/qemudocs.h"
#include "ui/referencepanel.h"
#include "ui/uiconfig.h"
#include "ui/widgets.h"

/* General */

GeneralPage::GeneralPage(Vm *vm, QWidget *parent) : SettingsPage(parent), m_name(new QLineEdit)
{
    auto *layout = new QVBoxLayout(this);
    auto *form = Widgets::form();
    auto *folder = Widgets::note(QString("<a href=\"%1\">%2</a>")
                            .arg(QUrl::fromLocalFile(vm->dir()).toString(),
                                 QDir::toNativeSeparators(vm->dir()).toHtmlEscaped()));

    m_name->setObjectName("name");
    form->addRow(tr("&Name:"), m_name);
    form->addRow(tr("Folder:"), folder);
    form->addRow(QString(), Widgets::hint(tr("The folder holds the arguments (vm.args), the disks the "
                                    "VM creates and its log.")));
    layout->addLayout(form);
    layout->addStretch();
}

QIcon GeneralPage::icon() const
{
    return Icons::themed({"preferences-system", "configure"}, QStyle::SP_ComputerIcon);
}

void GeneralPage::load(const ArgsFile &args)
{
    m_loaded = VmConfig::name(args);
    m_name->setText(m_loaded);
}

void GeneralPage::save(ArgsFile &args)
{
    const QString name = m_name->text().trimmed();

    if (!name.isEmpty() && name != m_loaded) {
        VmConfig::setName(args, name);
        m_loaded = name;
    }
}

/* System */

SystemPage::SystemPage(QWidget *parent)
    : SettingsPage(parent), m_memorySlider(new QSlider(Qt::Horizontal)), m_memory(new QSpinBox),
      m_cpuSlider(new QSlider(Qt::Horizontal)), m_cpus(new QSpinBox),
      m_topology(new QCheckBox(tr("Set the &topology"))), m_sockets(new QSpinBox),
      m_cores(new QSpinBox), m_threads(new QSpinBox), m_model(new QComboBox),
      m_modelInfo(Widgets::hint()), m_machine(new QComboBox), m_machineInfo(Widgets::hint()),
      m_accel(new QComboBox)
{
    auto *layout = new QVBoxLayout(this);
    auto *form = Widgets::form();
    auto *memoryRow = new QHBoxLayout;
    auto *cpuRow = new QHBoxLayout;
    auto *topologyRow = new QHBoxLayout;
    const qint64 hostMiB = Widgets::hostMemoryMiB();
    const int hostCpus = QThread::idealThreadCount();

    m_memory->setObjectName("memory");
    m_memory->setRange(64, int(qMax<qint64>(hostMiB, 1024)));
    m_memory->setSingleStep(256);
    m_memory->setSuffix(tr(" MiB"));
    m_memory->setMinimumWidth(m_memory->fontMetrics().horizontalAdvance("0000000 MiB") + 40);
    m_memorySlider->setRange(0, m_memory->maximum() / 256);
    m_memorySlider->setPageStep(4);
    Widgets::link(m_memorySlider, m_memory, 256);
    memoryRow->addWidget(m_memorySlider, 1);
    memoryRow->addWidget(m_memory);

    m_cpus->setObjectName("cpus");
    m_cpus->setRange(1, qMax(hostCpus, 1));
    m_cpuSlider->setRange(1, m_cpus->maximum());
    m_cpuSlider->setPageStep(2);
    Widgets::link(m_cpuSlider, m_cpus, 1);
    cpuRow->addWidget(m_cpuSlider, 1);
    cpuRow->addWidget(m_cpus);

    for (QSpinBox *spin : {m_sockets, m_cores, m_threads}) {
        spin->setRange(1, 1024);
        spin->setEnabled(false);
        connect(spin, &QSpinBox::valueChanged, this, &SystemPage::updateTopology);
    }
    topologyRow->addWidget(m_topology);
    topologyRow->addSpacing(12);
    topologyRow->addWidget(new QLabel(tr("Sockets:")));
    topologyRow->addWidget(m_sockets);
    topologyRow->addWidget(new QLabel(tr("Cores:")));
    topologyRow->addWidget(m_cores);
    topologyRow->addWidget(new QLabel(tr("Threads:")));
    topologyRow->addWidget(m_threads);
    topologyRow->addStretch();
    connect(m_topology, &QCheckBox::toggled, this, [this](bool on) {
        for (QSpinBox *spin : {m_sockets, m_cores, m_threads}) {
            spin->setEnabled(on);
        }
        if (on && m_sockets->value() * m_cores->value() * m_threads->value() !=
                      m_cpus->value()) {
            /* one socket of cores */
            const QSignalBlocker a(m_sockets), b(m_cores), c(m_threads);
            m_sockets->setValue(1);
            m_cores->setValue(m_cpus->value());
            m_threads->setValue(1);
        }
        updateTopology();
    });

    m_model->setObjectName("cpuModel");
    m_model->setEditable(true);
    m_model->setInsertPolicy(QComboBox::NoInsert);
    m_model->lineEdit()->setPlaceholderText(tr("QEMU default"));
    m_machine->setObjectName("machine");
    m_machine->setEditable(true);
    m_machine->setInsertPolicy(QComboBox::NoInsert);
    m_accel->setObjectName("accel");
    connect(m_model, &QComboBox::currentTextChanged, this, &SystemPage::describe);
    connect(m_machine, &QComboBox::currentTextChanged, this, &SystemPage::describe);

    form->addRow(Widgets::label(tr("&Memory:"), m_memory), memoryRow);
    form->addRow(QString(), Widgets::hint(tr("This computer has %1 GiB.")
                                     .arg(QString::number(hostMiB / 1024.0, 'f', 1))));
    form->addRow(Widgets::label(tr("&Processors:"), m_cpus), cpuRow);
    form->addRow(QString(), topologyRow);
    form->addRow(tr("Processor &model:"), m_model);
    form->addRow(QString(), m_modelInfo);
    form->addRow(tr("Mac&hine:"), m_machine);
    form->addRow(QString(), m_machineInfo);
    form->addRow(tr("&Acceleration:"), m_accel);
    layout->addLayout(form);
    layout->addStretch();

    fillLists();
    connect(QemuDocs::instance(), &QemuDocs::changed, this, &SystemPage::fillLists);
}

QIcon SystemPage::icon() const
{
    return Icons::themed({"cpu", "computer"}, QStyle::SP_ComputerIcon);
}

void SystemPage::fillLists()
{
    const QemuInfo *info = QemuDocs::instance()->info();
    const QString model = m_model->currentText();
    const QString machine = m_machine->currentText();
    const QSignalBlocker a(m_model), b(m_machine);
    QStringList models = {"host", "max"};
    QStringList machines = {"q35", "pc"};

    if (info) {
        for (const QemuNamedDoc &c : info->cpus) {
            if (!models.contains(c.name)) {
                models << c.name;
            }
        }
        for (const QemuNamedDoc &m : info->machines) {
            if (!machines.contains(m.name)) {
                machines << m.name;
            }
        }
    }
    m_model->clear();
    m_model->addItems(models);
    m_model->setCurrentText(model);
    m_machine->clear();
    m_machine->addItems(machines);
    m_machine->setCurrentText(machine);
    describe();
}

void SystemPage::describe()
{
    const QemuInfo *info = QemuDocs::instance()->info();
    const QString model = m_model->currentText().trimmed();
    const QString machine = m_machine->currentText().trimmed();
    QString modelText, machineText;

    if (model.isEmpty()) {
        modelText = tr("QEMU's basic processor, for compatibility.");
    } else if (model == "host") {
        modelText = tr("The processor of this computer with all its features: the fastest. "
                       "Needs KVM.");
    } else if (model == "max") {
        modelText = tr("Every feature QEMU can offer.");
    } else if (info) {
        for (const QemuNamedDoc &c : info->cpus) {
            if (c.name == model) {
                modelText = c.desc;
            }
        }
    }
    if (machine == "q35") {
        machineText = tr("A modern PC with PCI Express. The best choice for most systems.");
    } else if (machine == "pc") {
        machineText = tr("An older PC (i440FX), for old systems.");
    } else if (info) {
        for (const QemuNamedDoc &m : info->machines) {
            if (m.name == machine) {
                machineText = m.desc;
            }
        }
    }
    m_modelInfo->setText(modelText);
    m_machineInfo->setText(machineText);
}

void SystemPage::updateTopology()
{
    if (m_topology->isChecked()) {
        m_cpus->setValue(m_sockets->value() * m_cores->value() * m_threads->value());
    }
    m_cpus->setEnabled(!m_topology->isChecked());
    m_cpuSlider->setEnabled(!m_topology->isChecked());
}

void SystemPage::load(const ArgsFile &args)
{
    const QString accel = UiConfig::accel(args);

    m_loadedMemory = VmConfig::memoryMiB(args);
    m_loadedCpus = VmConfig::cpus(args);
    m_loadedMachine = UiConfig::machineType(args);
    m_loadedAccel = accel;

    m_memory->setMaximum(int(qMax<qint64>(m_memory->maximum(), m_loadedMemory)));
    m_memorySlider->setMaximum(m_memory->maximum() / 256);
    m_memory->setValue(int(m_loadedMemory > 0 ? m_loadedMemory : 128));
    m_cpus->setMaximum(qMax(m_cpus->maximum(), m_loadedCpus.count));
    m_cpuSlider->setMaximum(m_cpus->maximum());
    m_cpus->setValue(m_loadedCpus.count);
    {
        const QSignalBlocker a(m_sockets), b(m_cores), c(m_threads), d(m_topology);
        const bool given = m_loadedCpus.sockets > 0 || m_loadedCpus.cores > 0 ||
                           m_loadedCpus.threads > 0;
        m_sockets->setValue(qMax(m_loadedCpus.sockets, 1));
        m_cores->setValue(qMax(m_loadedCpus.cores, 1));
        m_threads->setValue(qMax(m_loadedCpus.threads, 1));
        m_topology->setChecked(given);
        for (QSpinBox *spin : {m_sockets, m_cores, m_threads}) {
            spin->setEnabled(given);
        }
    }
    m_cpus->setEnabled(!m_topology->isChecked());
    m_cpuSlider->setEnabled(!m_topology->isChecked());
    m_model->setCurrentText(m_loadedCpus.model);
    m_machine->setCurrentText(m_loadedMachine);

    m_accel->clear();
    m_accel->addItem(tr("KVM: hardware virtualization, fast"), "kvm");
    m_accel->addItem(tr("TCG: software emulation, slow"), "tcg");
    if (accel.isEmpty()) {
        m_accel->addItem(tr("Not set: QEMU's default, TCG"), QString());
    } else if (accel != "kvm" && accel != "tcg") {
        m_accel->addItem(accel, accel);
    }
    m_accel->setCurrentIndex(m_accel->findData(accel));
    describe();
}

void SystemPage::save(ArgsFile &args)
{
    VmConfig::Cpus cpus;
    const QString machine = m_machine->currentText().trimmed();
    const QString accel = m_accel->currentData().toString();

    if (m_memory->value() != m_loadedMemory) {
        VmConfig::setMemoryMiB(args, m_memory->value());
        m_loadedMemory = m_memory->value();
    }

    cpus.count = m_cpus->value();
    if (m_topology->isChecked()) {
        cpus.sockets = m_sockets->value();
        cpus.cores = m_cores->value();
        cpus.threads = m_threads->value();
    }
    /* "host,topoext=on": the model, then flags for the -cpu line */
    const QString model = m_model->currentText().trimmed();
    const OptionValue flags(model.section(',', 1));
    cpus.model = model.section(',', 0, 0).trimmed();
    if (cpus.count != m_loadedCpus.count || cpus.sockets != m_loadedCpus.sockets ||
        cpus.cores != m_loadedCpus.cores || cpus.threads != m_loadedCpus.threads ||
        cpus.model != m_loadedCpus.model) {
        VmConfig::setCpus(args, cpus);
        m_loadedCpus = cpus;
    }
    if (!cpus.model.isEmpty() && !flags.isEmpty()) {
        const int cpu = args.indexOf("cpu");
        OptionValue v = args.valueAt(cpu);
        QString extra;

        for (const OptionValue::Item &item : flags.items()) {
            if (item.key.isEmpty() || item.bare) {
                /* +avx, or a flag written without a value */
                const QString word = item.key.isEmpty() ? item.value : item.key;
                if (!v.has(word) && !word.isEmpty()) {
                    extra += ',' + OptionValue::escape(word);
                }
            } else {
                v.set(item.key, item.value);
            }
        }
        args.setValueAt(cpu, v.toString() + extra);
        m_model->setCurrentText(cpus.model);
    }
    if (!machine.isEmpty() && machine != m_loadedMachine) {
        UiConfig::setMachineType(args, machine);
        m_loadedMachine = machine;
    }
    if (accel != m_loadedAccel) {
        UiConfig::setAccel(args, accel);
        m_loadedAccel = accel;
    }
}

/* Shared folders */

static const char *const kCacheModes[][2] = {
    {"auto", QT_TRANSLATE_NOOP("SharesPage", "Automatic")},
    {"always", QT_TRANSLATE_NOOP("SharesPage", "Always")},
    {"never", QT_TRANSLATE_NOOP("SharesPage", "Never")},
};

static QString cacheName(const QString &mode)
{
    for (const auto &m : kCacheModes) {
        if (mode == m[0]) {
            return QCoreApplication::translate("SharesPage", m[1]);
        }
    }
    return mode;
}

static bool sameShares(const QList<VmConfig::Share> &a, const QList<VmConfig::Share> &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (qsizetype i = 0; i < a.size(); i++) {
        if (a[i].tag != b[i].tag || a[i].path != b[i].path || a[i].cache != b[i].cache ||
            a[i].readonly != b[i].readonly) {
            return false;
        }
    }
    return true;
}

SharesPage::SharesPage(QWidget *parent)
    : SettingsPage(parent), m_virtiofsd(new Banner(Banner::Warning)),
      m_memory(new Banner(Banner::Information)), m_table(new QTableWidget(0, 4)),
      m_edit(new QPushButton(tr("&Edit…"))), m_remove(new QPushButton(tr("&Remove"))),
      m_mount(new QLabel)
{
    auto *layout = new QVBoxLayout(this);
    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton(tr("&Add…"));
    auto *guest = new QGroupBox(tr("In the guest"));
    auto *guestLayout = new QVBoxLayout(guest);

    m_table->setObjectName("shares");
    m_table->setHorizontalHeaderLabels(
        {tr("Name in the guest"), tr("Folder"), tr("Cache"), tr("Access")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setWordWrap(false);

    add->setIcon(Icons::themed({"list-add"}, QStyle::SP_FileDialogNewFolder));
    m_remove->setIcon(Icons::themed({"list-remove", "edit-delete"}, QStyle::SP_TrashIcon));
    buttons->addWidget(add);
    buttons->addWidget(m_edit);
    buttons->addWidget(m_remove);
    buttons->addStretch();

    m_mount->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_mount->setTextInteractionFlags(Qt::TextSelectableByMouse);
    guestLayout->addWidget(m_mount);
    guestLayout->addWidget(Widgets::hint(tr("Windows guests need the virtio-win drivers and WinFsp.")));

    layout->addWidget(m_virtiofsd);
    layout->addWidget(m_memory);
    layout->addWidget(Widgets::note(tr("Folders of this computer the VM can use. They are shared "
                              "with virtiofs, which is fast and works with Linux 5.4 or "
                              "later in the guest.")));
    layout->addWidget(m_table, 1);
    layout->addLayout(buttons);
    layout->addWidget(guest);

    m_virtiofsd->setText(tr("virtiofsd was not found, so shared folders will not work. "
                            "Install it with <code>sudo dnf install virtiofsd</code>, or "
                            "set its path in the preferences."));
    m_memory->button()->setText(tr("&Use Shared Memory"));
    connect(m_memory->button(), &QPushButton::clicked, this, [this]() {
        m_fixMemory = true;
        updateHints();
    });

    connect(add, &QPushButton::clicked, this, [this]() { edit(-1); });
    connect(m_edit, &QPushButton::clicked, this, [this]() { edit(m_table->currentRow()); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) { edit(row); });
    connect(m_remove, &QPushButton::clicked, this, [this]() {
        const int row = m_table->currentRow();
        if (row >= 0) {
            m_shares.removeAt(row);
            fill();
        }
    });
    connect(m_table, &QTableWidget::currentCellChanged, this, &SharesPage::updateHints);
}

QIcon SharesPage::icon() const
{
    return Icons::themed({"folder-network", "folder-remote"}, QStyle::SP_DirIcon);
}

void SharesPage::load(const ArgsFile &args)
{
    m_shares = VmConfig::shares(args);
    m_loaded = m_shares;
    m_sharedMemory = VmConfig::hasSharedMemory(args);
    m_fixMemory = false;
    fill();
}

void SharesPage::save(ArgsFile &args)
{
    if (!sameShares(m_shares, m_loaded)) {
        VmConfig::setShares(args, m_shares);
        m_loaded = m_shares;
    }
    if (!m_shares.isEmpty() && m_fixMemory && !VmConfig::hasSharedMemory(args)) {
        VmConfig::useSharedMemory(args);
    }
    m_sharedMemory = VmConfig::hasSharedMemory(args);
    m_fixMemory = false;
}

void SharesPage::fill()
{
    const int row = m_table->currentRow();

    m_table->setRowCount(int(m_shares.size()));
    for (int i = 0; i < m_shares.size(); i++) {
        const VmConfig::Share &s = m_shares[i];
        m_table->setItem(i, 0, new QTableWidgetItem(s.tag));
        m_table->setItem(i, 1, new QTableWidgetItem(QDir::toNativeSeparators(s.path)));
        m_table->setItem(i, 2, new QTableWidgetItem(cacheName(s.cache)));
        m_table->setItem(i, 3, new QTableWidgetItem(s.readonly ? tr("Read only")
                                                               : tr("Read and write")));
        m_table->item(i, 1)->setToolTip(s.path);
    }
    if (!m_shares.isEmpty()) {
        m_table->setCurrentCell(qBound(0, row, int(m_shares.size()) - 1), 0);
    }
    updateHints();
}

void SharesPage::updateHints()
{
    const int row = m_table->currentRow();
    const bool selected = row >= 0 && row < m_shares.size();

    m_edit->setEnabled(selected);
    m_remove->setEnabled(selected);
    m_virtiofsd->setVisible(!m_shares.isEmpty() && Paths::virtiofsd().isEmpty());

    if (m_shares.isEmpty() || m_sharedMemory) {
        m_memory->hide();
    } else if (m_fixMemory) {
        m_memory->setText(tr("The guest memory will come from shared memory (memfd), which "
                             "virtiofs needs. Its size stays the same."));
        m_memory->button()->hide();
        m_memory->show();
    } else {
        m_memory->setText(tr("virtiofs needs the guest memory to be shared memory, which it "
                             "is not in the arguments of this VM."));
        m_memory->button()->show();
        m_memory->show();
    }

    const QString tag = selected ? m_shares[row].tag : QString("TAG");
    const QString mount = "/mnt/" + tag;
    m_mount->setText(tr("Mount it:\n"
                        "  sudo mkdir -p %1\n"
                        "  sudo mount -t virtiofs %2 %1\n"
                        "\n"
                        "Or mount it at boot, with this line in /etc/fstab:\n"
                        "  %2  %1  virtiofs  defaults,nofail  0  0")
                         .arg(mount, tag));
}

void SharesPage::edit(int row)
{
    QStringList others;
    const bool adding = row < 0 || row >= m_shares.size();

    for (int i = 0; i < m_shares.size(); i++) {
        if (i != row) {
            others << m_shares[i].tag;
        }
    }
    ShareDialog dialog(adding ? VmConfig::Share() : m_shares[row], others, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (adding) {
        m_shares << dialog.share();
        if (!m_sharedMemory) {
            /* virtiofs needs it: say so rather than ask */
            m_fixMemory = true;
        }
        fill();
        m_table->setCurrentCell(int(m_shares.size()) - 1, 0);
    } else {
        m_shares[row] = dialog.share();
        fill();
    }
}

ShareDialog::ShareDialog(const VmConfig::Share &share, const QStringList &otherTags,
                         QWidget *parent)
    : QDialog(parent), m_path(new QLineEdit), m_tag(new QLineEdit), m_cache(new QComboBox),
      m_cacheInfo(Widgets::hint()), m_readonly(new QCheckBox(tr("&Read only: the VM cannot change "
                                                       "the files"))),
      m_error(new QLabel), m_otherTags(otherTags)
{
    auto *layout = new QVBoxLayout(this);
    auto *form = Widgets::form();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    setWindowTitle(share.tag.isEmpty() ? tr("Add a Shared Folder") : tr("Edit Shared Folder"));
    m_ok = buttons->button(QDialogButtonBox::Ok);
    m_path->setObjectName("path");
    m_tag->setObjectName("tag");
    for (const auto &mode : kCacheModes) {
        m_cache->addItem(QCoreApplication::translate("SharesPage", mode[1]), mode[0]);
    }
    m_error->setWordWrap(true);
    {
        QPalette palette = m_error->palette();
        palette.setColor(QPalette::WindowText, QColor(0xda, 0x44, 0x53));
        m_error->setPalette(palette);
    }

    form->addRow(Widgets::label(tr("&Folder:"), m_path),
                 Widgets::browseRow(m_path, tr("Folder to Share"), {}, true));
    form->addRow(tr("&Name in the guest:"), m_tag);
    form->addRow(QString(), Widgets::hint(tr("The guest mounts the folder by this name.")));
    form->addRow(tr("&Cache:"), m_cache);
    form->addRow(QString(), m_cacheInfo);
    form->addRow(QString(), m_readonly);
    layout->addLayout(form);
    layout->addWidget(m_error);
    layout->addStretch();
    layout->addWidget(buttons);

    m_path->setText(share.path);
    m_tag->setText(share.tag);
    m_tagEdited = !share.tag.isEmpty();
    m_cache->setCurrentIndex(qMax(0, m_cache->findData(share.cache)));
    m_readonly->setChecked(share.readonly);

    connect(m_path, &QLineEdit::textChanged, this, [this](const QString &path) {
        if (!m_tagEdited) {
            static const QRegularExpression unsafe("[^A-Za-z0-9_.-]+");
            QString tag = QFileInfo(QDir::cleanPath(path)).fileName();
            tag.replace(unsafe, "-");
            const QSignalBlocker block(m_tag);
            m_tag->setText(tag.left(36));
        }
        validate();
    });
    connect(m_tag, &QLineEdit::textEdited, this, [this]() { m_tagEdited = true; });
    connect(m_tag, &QLineEdit::textChanged, this, &ShareDialog::validate);
    auto describeCache = [this]() {
        const QString mode = m_cache->currentData().toString();
        if (mode == "auto") {
            m_cacheInfo->setText(tr("The guest keeps files in its cache for a moment: changes "
                                    "made on this computer show up within a second."));
        } else if (mode == "always") {
            m_cacheInfo->setText(tr("The fastest. The guest keeps files in its cache, so "
                                    "changes made on this computer may not show up in the "
                                    "guest. Best when only the VM uses the folder."));
        } else {
            m_cacheInfo->setText(tr("Always up to date with this computer, but slower."));
        }
    };
    connect(m_cache, &QComboBox::currentIndexChanged, this, describeCache);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    describeCache();
    validate();
    resize(560, sizeHint().height());
}

VmConfig::Share ShareDialog::share() const
{
    VmConfig::Share s;

    s.tag = m_tag->text().trimmed();
    s.path = QDir::cleanPath(m_path->text().trimmed());
    s.cache = m_cache->currentData().toString();
    s.readonly = m_readonly->isChecked();
    return s;
}

void ShareDialog::validate()
{
    static const QRegularExpression valid("^[A-Za-z0-9_.-]{1,36}$");
    const QString path = m_path->text().trimmed();
    const QString tag = m_tag->text().trimmed();
    QString error;

    if (path.isEmpty()) {
        error = " ";
    } else if (!QFileInfo(path).isDir()) {
        error = tr("This folder does not exist.");
    } else if (tag.isEmpty()) {
        error = tr("Give the folder a name for the guest.");
    } else if (!valid.match(tag).hasMatch()) {
        error = tr("The name can have up to 36 letters, digits, dots, dashes and "
                   "underscores.");
    } else if (m_otherTags.contains(tag)) {
        error = tr("Another shared folder has this name.");
    }
    m_error->setText(error.trimmed());
    m_ok->setEnabled(error.isEmpty());
}

/* PCI */

PciPage::PciPage(QWidget *parent)
    : SettingsPage(parent), m_banner(new Banner(Banner::Warning)), m_tree(new QTreeWidget)
{
    auto *layout = new QVBoxLayout(this);

    m_tree->setObjectName("pci");
    m_tree->setHeaderLabels({tr("Device"), tr("Driver"), tr("Status")});
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setItemsExpandable(false);

    layout->addWidget(m_banner);
    layout->addWidget(Widgets::note(tr("A device passed through belongs to the VM, which drives it "
                              "directly: this computer cannot use it while the VM runs. "
                              "The devices of an IOMMU group go together, and the "
                              "functions of a card, such as a graphics card and its "
                              "sound, are checked together.")));
    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        const QString address = item->data(0, Qt::UserRole).toString();
        if (column != 0 || address.isEmpty() || item->checkState(0) != Qt::Checked) {
            return;
        }
        /* the other functions of the card: 0000:03:00.0 and 0000:03:00.1 */
        const QString slot = address.section('.', 0, 0);
        const QSignalBlocker block(m_tree);
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
            const QString other = (*it)->data(0, Qt::UserRole).toString();
            if (!other.isEmpty() && other.section('.', 0, 0) == slot) {
                (*it)->setCheckState(0, Qt::Checked);
            }
        }
    });
}

QIcon PciPage::icon() const
{
    return Icons::themed({"preferences-desktop-peripherals", "video-display"},
                         QStyle::SP_DriveHDIcon);
}

void PciPage::load(const ArgsFile &args)
{
    const QList<PciDevice> devices = HostDevices::pciDevices();
    const qint64 memory = VmConfig::memoryMiB(args);
    QMap<int, QTreeWidgetItem *> groups;
    QStringList missing;
    const QSignalBlocker block(m_tree);

    m_loaded = VmConfig::pciPassthrough(args);
    missing = m_loaded;
    m_tree->clear();

    if (!HostDevices::iommuEnabled()) {
        m_banner->setText(tr("The IOMMU is off, so no device can be passed through. Turn on "
                             "VT-d (Intel) or AMD-Vi in the firmware settings of this "
                             "computer, and add <code>intel_iommu=on</code> or "
                             "<code>amd_iommu=on</code> to the kernel command line."));
        m_banner->show();
    } else if (devices.isEmpty()) {
        m_banner->setText(tr("No PCI devices were found."));
        m_banner->show();
    } else {
        m_banner->hide();
    }

    for (const PciDevice &dev : devices) {
        if ((dev.classCode >> 16) == 0x06) {
            /* bridges stay with the host */
            continue;
        }
        QTreeWidgetItem *&group = groups[dev.iommuGroup];
        if (!group) {
            group = new QTreeWidgetItem(
                {dev.iommuGroup < 0 ? tr("No IOMMU group")
                                    : tr("IOMMU group %1").arg(dev.iommuGroup)});
            group->setFlags(Qt::ItemIsEnabled);
            group->setFirstColumnSpanned(true);
        }

        const QStringList problems = HostDevices::pciProblems(dev, memory);
        auto *item = new QTreeWidgetItem(group, {dev.displayName(), dev.driver,
                                                 problems.isEmpty() ? tr("Ready")
                                                                    : problems.first()});
        item->setData(0, Qt::UserRole, dev.address);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(0, m_loaded.contains(dev.address) ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(0, QString("%1 [%2:%3]\n%4")
                                .arg(dev.address)
                                .arg(dev.vendorId, 4, 16, QChar('0'))
                                .arg(dev.deviceId, 4, 16, QChar('0'))
                                .arg(dev.className));
        item->setToolTip(2, problems.isEmpty() ? tr("Ready to be passed through")
                                               : problems.join('\n'));
        missing.removeAll(dev.address);
    }
    for (QTreeWidgetItem *group : std::as_const(groups)) {
        m_tree->addTopLevelItem(group);
        group->setExpanded(true);
    }
    if (!missing.isEmpty()) {
        auto *group = new QTreeWidgetItem(m_tree, {tr("Not on this computer")});
        group->setFlags(Qt::ItemIsEnabled);
        group->setFirstColumnSpanned(true);
        for (const QString &address : std::as_const(missing)) {
            auto *item = new QTreeWidgetItem(group, {address, QString(), tr("Not found")});
            item->setData(0, Qt::UserRole, address);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            item->setCheckState(0, Qt::Checked);
        }
        group->setExpanded(true);
    }
    if (m_tree->topLevelItemCount() == 0) {
        auto *empty = new QTreeWidgetItem(m_tree, {tr("No devices to show")});
        empty->setFlags(Qt::NoItemFlags);
    }
    m_tree->resizeColumnToContents(0);
    m_tree->resizeColumnToContents(1);
}

QStringList PciPage::checked() const
{
    QStringList list;

    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        const QString address = (*it)->data(0, Qt::UserRole).toString();
        if (!address.isEmpty() && (*it)->checkState(0) == Qt::Checked) {
            list << address;
        }
    }
    return list;
}

void PciPage::save(ArgsFile &args)
{
    const QStringList list = checked();

    if (list != m_loaded) {
        VmConfig::setPciPassthrough(args, list);
        m_loaded = list;
    }
}

/* USB */

UsbPage::UsbPage(QWidget *parent)
    : SettingsPage(parent), m_controller(new Banner(Banner::Warning)), m_tree(new QTreeWidget)
{
    auto *layout = new QVBoxLayout(this);

    m_tree->setObjectName("usb");
    m_tree->setHeaderLabels({tr("Device"), tr("ID"), tr("Status")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(true);

    m_controller->button()->setText(tr("&Add a USB Controller"));
    connect(m_controller->button(), &QPushButton::clicked, this, [this]() {
        m_addController = true;
        m_controller->button()->hide();
        m_controller->setText(tr("A USB 3 controller (qemu-xhci) will be added."));
    });

    layout->addWidget(m_controller);
    layout->addWidget(Widgets::note(tr("The checked devices are given to the VM when it starts, by "
                              "their vendor and product ID: this computer cannot use them "
                              "while the VM runs. The menu of the VM window can attach "
                              "devices while it runs too.")));
    layout->addWidget(m_tree, 1);
}

QIcon UsbPage::icon() const
{
    return Icons::themed({"drive-removable-media-usb", "media-removable"},
                         QStyle::SP_DriveFDIcon);
}

void UsbPage::load(const ArgsFile &args)
{
    const QList<UsbDevice> devices = HostDevices::usbDevices();
    QList<VmConfig::UsbId> missing;

    m_loaded = VmConfig::usbPassthrough(args);
    missing = m_loaded;
    m_addController = false;
    m_tree->clear();

    if (VmConfig::hasUsbController(args)) {
        m_controller->hide();
    } else {
        m_controller->setText(tr("This VM has no USB controller, so it cannot use USB "
                                 "devices."));
        m_controller->button()->show();
        m_controller->show();
    }

    for (const UsbDevice &dev : devices) {
        const VmConfig::UsbId id{dev.vendorId, dev.productId};
        const bool access = QFileInfo(dev.devNode()).isWritable();
        QString name = QString("%1 %2").arg(dev.manufacturer, dev.product).simplified();

        if (dev.isHub) {
            continue;
        }
        if (name.isEmpty()) {
            name = tr("Unknown device");
        }
        auto *item = new QTreeWidgetItem(
            m_tree, {name,
                     QString("%1:%2")
                         .arg(dev.vendorId, 4, 16, QChar('0'))
                         .arg(dev.productId, 4, 16, QChar('0')),
                     access ? tr("Ready") : tr("No access")});
        item->setData(0, Qt::UserRole, dev.vendorId);
        item->setData(0, Qt::UserRole + 1, dev.productId);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(0, m_loaded.contains(id) ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(0, tr("Bus %1, port %2").arg(dev.bus).arg(dev.port));
        if (!access) {
            item->setToolTip(
                2, tr("QEMU cannot open %1. Give your user access with a udev rule, e.g. in "
                      "/etc/udev/rules.d/70-qemu-usb.rules:\n"
                      "SUBSYSTEM==\"usb\", ATTR{idVendor}==\"%2\", ATTR{idProduct}==\"%3\", "
                      "TAG+=\"uaccess\"")
                       .arg(dev.devNode())
                       .arg(dev.vendorId, 4, 16, QChar('0'))
                       .arg(dev.productId, 4, 16, QChar('0')));
        }
        missing.removeAll(id);
    }
    for (const VmConfig::UsbId &id : std::as_const(missing)) {
        auto *item = new QTreeWidgetItem(
            m_tree, {tr("Not connected"),
                     QString("%1:%2")
                         .arg(id.vendor, 4, 16, QChar('0'))
                         .arg(id.product, 4, 16, QChar('0')),
                     tr("Attached when connected before the VM starts")});
        item->setData(0, Qt::UserRole, id.vendor);
        item->setData(0, Qt::UserRole + 1, id.product);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
    }
    if (m_tree->topLevelItemCount() == 0) {
        auto *empty = new QTreeWidgetItem(m_tree, {tr("No USB devices found")});
        empty->setFlags(Qt::NoItemFlags);
    }
    m_tree->resizeColumnToContents(0);
    m_tree->resizeColumnToContents(1);
}

QList<VmConfig::UsbId> UsbPage::checked() const
{
    QList<VmConfig::UsbId> list;

    for (int i = 0; i < m_tree->topLevelItemCount(); i++) {
        const QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->flags() & Qt::ItemIsUserCheckable && item->checkState(0) == Qt::Checked) {
            list << VmConfig::UsbId{quint16(item->data(0, Qt::UserRole).toUInt()),
                                    quint16(item->data(0, Qt::UserRole + 1).toUInt())};
        }
    }
    return list;
}

void UsbPage::save(ArgsFile &args)
{
    const QList<VmConfig::UsbId> list = checked();

    if (list != m_loaded) {
        VmConfig::setUsbPassthrough(args, list);
        m_loaded = list;
    }
    if (m_addController && !VmConfig::hasUsbController(args)) {
        args.add("device", "qemu-xhci");
    }
    m_addController = false;
}

/* Arguments */

ArgumentsPage::ArgumentsPage(QWidget *parent)
    : SettingsPage(parent), m_pane(new ArgsEditorPane)
{
    auto *layout = new QVBoxLayout(this);
    auto *splitter = new QSplitter(Qt::Horizontal);
    auto *reference = new ReferencePanel;

    m_pane->setObjectName("argsPane");
    reference->setObjectName("reference");
    reference->setEditor(m_pane->editor());
    splitter->addWidget(m_pane);
    splitter->addWidget(reference);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setChildrenCollapsible(false);

    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(Widgets::note(tr("The QEMU command line of this VM, one option per line; lines "
                              "starting with # are comments. The other pages edit these "
                              "same lines. Ctrl+Space completes option and device names.")));
    layout->addWidget(splitter, 1);
}

QIcon ArgumentsPage::icon() const
{
    return Icons::themed({"utilities-terminal", "text-x-script"}, QStyle::SP_FileIcon);
}

void ArgumentsPage::load(const ArgsFile &args)
{
    m_loaded = args.toText();
    m_pane->editor()->setPlainText(m_loaded);
}

void ArgumentsPage::save(ArgsFile &args)
{
    const QString text = m_pane->editor()->toPlainText();

    if (text != m_loaded) {
        args = ArgsFile::parse(text);
        m_loaded = text;
    }
}
