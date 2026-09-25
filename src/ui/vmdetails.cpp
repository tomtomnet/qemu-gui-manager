// SPDX-License-Identifier: GPL-2.0-or-later
#include "vmdetails.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include "core/hostdevices.h"
#include "core/qemuinfo.h"
#include "core/vmconfig.h"
#include "core/vmrunner.h"
#include "core/vmstore.h"
#include "ui/banner.h"
#include "ui/icons.h"
#include "ui/qemudocs.h"
#include "ui/uiconfig.h"

QString stateText(const Vm *vm)
{
    switch (vm->runner()->state()) {
    case VmRunner::State::Stopped:
        break;
    case VmRunner::State::Starting:
        return QCoreApplication::translate("VmDetails", "Starting…");
    case VmRunner::State::Running:
        return QCoreApplication::translate("VmDetails", "Running");
    case VmRunner::State::Paused:
        return QCoreApplication::translate("VmDetails", "Paused");
    case VmRunner::State::Stopping:
        return QCoreApplication::translate("VmDetails", "Shutting down…");
    }
    return QCoreApplication::translate("VmDetails", "Powered off");
}

static QString sizeText(qint64 mib)
{
    return mib % 1024 == 0 ? QCoreApplication::translate("VmDetails", "%1 GiB").arg(mib / 1024)
                           : QCoreApplication::translate("VmDetails", "%1 MiB").arg(mib);
}

static QString link(const QString &path)
{
    return QString("<a href=\"%1\">%2</a>")
        .arg(QUrl::fromLocalFile(path).toString().toHtmlEscaped(),
             QDir::toNativeSeparators(path).toHtmlEscaped());
}

VmDetails::VmDetails(QWidget *parent)
    : QWidget(parent), m_icon(new QLabel), m_name(new QLabel), m_state(new QLabel),
      m_error(new Banner(Banner::Warning)), m_text(new QTextBrowser)
{
    auto *layout = new QVBoxLayout(this);
    auto *header = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    QFont font = m_name->font();

    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.4);
    m_name->setFont(font);
    m_name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_icon->setPixmap(Icons::themed({"computer"}, QStyle::SP_ComputerIcon).pixmap(48, 48));
    titles->addWidget(m_name);
    titles->addWidget(m_state);
    header->addWidget(m_icon);
    header->addLayout(titles, 1);

    m_error->button()->setText(tr("Show &Log"));
    m_error->button()->show();
    m_error->hide();
    m_text->setObjectName("details");
    m_text->setOpenExternalLinks(true);
    m_text->setFrameShape(QFrame::NoFrame);

    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(header);
    layout->addWidget(m_error);
    layout->addWidget(m_text, 1);

    connect(m_error->button(), &QPushButton::clicked, this, &VmDetails::showLog);
    connect(QemuDocs::instance(), &QemuDocs::changed, this, &VmDetails::refresh);
}

void VmDetails::setVm(Vm *vm)
{
    m_vm = vm;
    refresh();
}

void VmDetails::setError(const QString &error)
{
    m_error->setText(error.toHtmlEscaped().replace('\n', "<br>"));
    m_error->setVisible(!error.isEmpty());
}

void VmDetails::refresh()
{
    if (!m_vm) {
        m_name->clear();
        m_state->clear();
        m_text->clear();
        return;
    }
    m_name->setText(m_vm->name());
    m_state->setText(stateText(m_vm));

    /* keep the scroll position across updates */
    const int scroll = m_text->verticalScrollBar()->value();
    m_text->setHtml(html());
    m_text->verticalScrollBar()->setValue(scroll);
}

QString VmDetails::html() const
{
    using Rows = QList<std::pair<QString, QString>>;
    const ArgsFile &args = m_vm->args();
    const QemuInfo *info = QemuDocs::instance()->info();
    const QString dim = palette().color(QPalette::PlaceholderText).name();
    QString html;

    auto section = [&](const QString &title, const Rows &rows) {
        if (rows.isEmpty()) {
            return;
        }
        html += QString("<h3>%1</h3><table cellspacing=\"0\" cellpadding=\"2\">")
                    .arg(title.toHtmlEscaped());
        for (const auto &[key, value] : rows) {
            html += QString("<tr><td style=\"color:%1\">%2&nbsp;&nbsp;&nbsp;</td>"
                            "<td>%3</td></tr>")
                        .arg(dim, key.toHtmlEscaped(), value);
        }
        html += "</table>";
    };
    auto text = [](const QString &plain) { return plain.toHtmlEscaped(); };

    /* System */
    const qint64 memory = VmConfig::memoryMiB(args);
    const VmConfig::Cpus cpus = VmConfig::cpus(args);
    const QString machine = UiConfig::machineType(args);
    const QString accel = UiConfig::accel(args);
    QString processors = QString::number(cpus.count);
    QString accelText;

    if (!cpus.model.isEmpty()) {
        processors += " · " + cpus.model;
    }
    if (cpus.sockets > 0 || cpus.cores > 0 || cpus.threads > 0) {
        processors += tr(" (%1 sockets, %2 cores, %3 threads)")
                          .arg(qMax(cpus.sockets, 1))
                          .arg(qMax(cpus.cores, 1))
                          .arg(qMax(cpus.threads, 1));
    }
    if (accel == "kvm") {
        accelText = tr("KVM");
    } else if (accel.isEmpty() || accel == "tcg") {
        accelText = tr("TCG, software emulation");
    } else {
        accelText = accel;
    }
    section(tr("System"),
            {{tr("Memory"), text(memory > 0 ? sizeText(memory) : tr("QEMU default"))},
             {tr("Processors"), text(processors)},
             {tr("Machine"), text(machine.isEmpty() ? tr("QEMU default") : machine)},
             {tr("Acceleration"), text(accelText)},
             {tr("Firmware"), text(UiConfig::firmwareSummary(args))}});

    section(tr("Display"), {{tr("Graphics"), text(UiConfig::displaySummary(args, info))}});

    /* Storage */
    Rows storage;
    for (const UiConfig::Disk &disk : UiConfig::disks(args)) {
        QString kind = disk.cdrom ? tr("CD/DVD") : tr("Disk");
        if (!disk.interface.isEmpty()) {
            kind += QString(" (%1)").arg(disk.interface);
        }
        storage << std::pair(kind, disk.file.isEmpty() ? text(tr("empty")) : text(disk.file));
    }
    if (storage.isEmpty()) {
        storage << std::pair(tr("Disks"), text(tr("none")));
    }
    section(tr("Storage"), storage);

    section(tr("Network and Sound"),
            {{tr("Network"), text(UiConfig::networkSummary(args, info))},
             {tr("Sound"), text(UiConfig::audioSummary(args, info))}});

    /* Shared folders */
    Rows shares;
    for (const VmConfig::Share &s : VmConfig::shares(args)) {
        shares << std::pair(s.tag, link(s.path) + (s.readonly ? text(tr(" · read only"))
                                                              : QString()));
    }
    section(tr("Shared Folders"), shares);

    /* Passthrough */
    const QList<VmConfig::UsbId> usbIds = VmConfig::usbPassthrough(args);
    Rows usb;
    if (!usbIds.isEmpty()) {
        const QList<UsbDevice> devices = HostDevices::usbDevices();
        for (const VmConfig::UsbId &id : usbIds) {
            QString name = tr("not connected");
            for (const UsbDevice &dev : devices) {
                if (dev.vendorId == id.vendor && dev.productId == id.product) {
                    name = QString("%1 %2").arg(dev.manufacturer, dev.product).simplified();
                }
            }
            usb << std::pair(QString("%1:%2")
                                 .arg(id.vendor, 4, 16, QChar('0'))
                                 .arg(id.product, 4, 16, QChar('0')),
                             text(name));
        }
        if (!VmConfig::hasUsbController(args)) {
            usb << std::pair(tr("Warning"), text(tr("no USB controller")));
        }
    }
    section(tr("USB Devices"), usb);

    const QStringList pciAddresses = VmConfig::pciPassthrough(args);
    Rows pci;
    if (!pciAddresses.isEmpty()) {
        const QList<PciDevice> devices = HostDevices::pciDevices();
        for (const QString &address : pciAddresses) {
            QString name = tr("not found");
            for (const PciDevice &dev : devices) {
                if (dev.address == address) {
                    name = QString("%1 %2").arg(dev.vendorName, dev.deviceName).simplified();
                }
            }
            pci << std::pair(address, text(name));
        }
    }
    section(tr("PCI Devices"), pci);

    /* Files */
    Rows files = {{tr("Folder"), link(m_vm->dir())}};
    if (QFileInfo::exists(m_vm->runner()->logPath())) {
        files << std::pair(tr("Log"), link(m_vm->runner()->logPath()));
    }
    section(tr("Files"), files);
    return html;
}
