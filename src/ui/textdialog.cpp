// SPDX-License-Identifier: GPL-2.0-or-later
#include "textdialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>

#include "ui/icons.h"

TextDialog::TextDialog(QWidget *parent, const QString &title, const QString &path)
    : QDialog(parent), m_text(new QPlainTextEdit), m_path(path)
{
    auto *layout = new QVBoxLayout(this);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(title);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_text);
    layout->addWidget(buttons);

    if (path.isEmpty()) {
        QPushButton *copy = buttons->addButton(tr("&Copy"), QDialogButtonBox::ActionRole);
        copy->setIcon(Icons::themed({"edit-copy"}, QStyle::SP_FileIcon));
        connect(copy, &QPushButton::clicked, this, [this]() {
            QApplication::clipboard()->setText(m_text->toPlainText());
        });
    } else {
        QPushButton *reload = buttons->addButton(tr("&Reload"), QDialogButtonBox::ActionRole);
        QPushButton *open = buttons->addButton(tr("&Open Externally"),
                                               QDialogButtonBox::ActionRole);
        reload->setIcon(Icons::themed({"view-refresh"}, QStyle::SP_BrowserReload));
        open->setIcon(Icons::themed({"document-open", "text-x-generic"}, QStyle::SP_FileIcon));
        connect(reload, &QPushButton::clicked, this, &TextDialog::reload);
        connect(open, &QPushButton::clicked, this, [this]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_path));
        });
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(900, 560);
}

void TextDialog::showText(QWidget *parent, const QString &title, const QString &text)
{
    auto *dialog = new TextDialog(parent, title, {});
    dialog->m_text->setPlainText(text);
    dialog->show();
}

void TextDialog::showFile(QWidget *parent, const QString &title, const QString &path)
{
    auto *dialog = new TextDialog(parent, title, path);
    dialog->reload();
    dialog->show();
}

void TextDialog::reload()
{
    QFile f(m_path);

    if (!f.open(QIODevice::ReadOnly)) {
        m_text->setPlainText(tr("%1 cannot be read: %2").arg(m_path, f.errorString()));
        return;
    }
    m_text->setPlainText(QString::fromUtf8(f.readAll()));
    m_text->verticalScrollBar()->setValue(m_text->verticalScrollBar()->maximum());
}
