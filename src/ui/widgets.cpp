// SPDX-License-Identifier: GPL-2.0-or-later
#include "widgets.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QSpinBox>

Form::Form()
{
    setColumnStretch(1, 1);
}

void Form::addLabel(QWidget *label, Qt::Alignment vertical)
{
    const int row = rowCount();
    const Qt::Alignment horizontal =
        Qt::Alignment(QApplication::style()->styleHint(QStyle::SH_FormLayoutLabelAlignment)) &
        Qt::AlignHorizontal_Mask;

    if (label) {
        addWidget(label, row, 0, horizontal | vertical);
    }
}

void Form::addRow(const QString &label, QWidget *field)
{
    QLabel *l = nullptr;

    if (!label.isEmpty()) {
        l = new QLabel(label);
        l->setBuddy(field);
    }
    addRow(l, field);
}

void Form::addRow(QWidget *label, QWidget *field)
{
    const int row = rowCount();
    const bool grows = field->sizePolicy().horizontalPolicy() & QSizePolicy::ExpandFlag;

    addLabel(label, Qt::AlignVCenter);
    addWidget(field, row, 1, grows ? Qt::Alignment() : Qt::AlignLeft);
}

void Form::addRow(const QString &label, QLayout *field)
{
    addRow(label.isEmpty() ? nullptr : new QLabel(label), field);
}

void Form::addRow(QWidget *label, QLayout *field)
{
    const int row = rowCount();

    /* a column of rows: the label goes with the first */
    addLabel(label, qobject_cast<QHBoxLayout *>(field) ? Qt::AlignVCenter : Qt::AlignTop);
    addLayout(field, row, 1);
}

namespace Widgets {

Form *form()
{
    return new Form;
}

QLabel *label(const QString &text, QWidget *buddy)
{
    auto *label = new QLabel(text);

    label->setBuddy(buddy);
    return label;
}

QLabel *note(const QString &text)
{
    auto *label = new QLabel(text);

    /* first: word wrap adds height for width to the policy */
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    label->setOpenExternalLinks(true);
    return label;
}

QLabel *hint(const QString &text)
{
    QLabel *label = note(text);
    QFont font = label->font();
    QPalette palette = label->palette();

    font.setPointSizeF(font.pointSizeF() * 0.9);
    label->setFont(font);
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
    return label;
}

QWidget *browseRow(QLineEdit *edit, const QString &title, const QString &filter, bool folder)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    auto *browse = new QPushButton(QObject::tr("Browse…"));

    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(edit, 1);
    layout->addWidget(browse);
    row->setFocusProxy(edit);
    QObject::connect(browse, &QPushButton::clicked, edit, [=]() {
        const QString current = edit->text().trimmed().isEmpty() ? edit->placeholderText()
                                                                  : edit->text().trimmed();
        const QString start = QFileInfo(current).isAbsolute()
                                  ? (folder ? current : QFileInfo(current).absolutePath())
                                  : QDir::homePath();
        const QString path =
            folder ? QFileDialog::getExistingDirectory(edit->window(), title, start)
                   : QFileDialog::getOpenFileName(edit->window(), title, start, filter);
        if (!path.isEmpty()) {
            edit->setText(path);
        }
    });
    return row;
}

void link(QSlider *slider, QSpinBox *spin, int unit)
{
    QObject::connect(slider, &QSlider::valueChanged, spin, [spin, unit](int value) {
        if (spin->value() / unit != value) {
            spin->setValue(value * unit);
        }
    });
    QObject::connect(spin, &QSpinBox::valueChanged, slider, [slider, unit](int value) {
        const QSignalBlocker block(slider);
        slider->setValue(value / unit);
    });
}

qint64 hostMemoryMiB()
{
    QFile f("/proc/meminfo");

    if (f.open(QIODevice::ReadOnly)) {
        const QRegularExpressionMatch m = QRegularExpression("MemTotal:\\s+(\\d+) kB")
                                              .match(QString::fromLatin1(f.readAll()));
        if (m.hasMatch()) {
            return m.captured(1).toLongLong() / 1024;
        }
    }
    return 16384;
}

}
