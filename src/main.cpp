// SPDX-License-Identifier: GPL-2.0-or-later
#include <QApplication>

#include "core/paths.h"
#include "core/vmrunner.h"
#include "core/vmstore.h"
#include "ui/icons.h"
#include "ui/mainwindow.h"
#include "ui/qemudocs.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QApplication::setApplicationName("qemu-gui-manager");
    QApplication::setApplicationDisplayName(QObject::tr("QEMU GUI Manager"));
    QApplication::setApplicationVersion(QGM_VERSION);
    QApplication::setDesktopFileName("qemu-gui-manager");
    QApplication::setWindowIcon(Icons::app());

    VmStore store(Paths::vmsDir());
    for (Vm *vm : store.vms()) {
        /* VMs outlive the manager: find those still running */
        vm->runner()->attach();
    }
    /* load the QEMU documentation in the background now */
    QemuDocs::instance();

    MainWindow window(&store);
    window.show();
    return app.exec();
}
