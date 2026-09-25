// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDialog>

class QPlainTextEdit;

/* Read-only text: a command line to copy, or a log file to follow */
class TextDialog : public QDialog
{
    Q_OBJECT

public:
    static void showText(QWidget *parent, const QString &title, const QString &text);
    static void showFile(QWidget *parent, const QString &title, const QString &path);

private:
    TextDialog(QWidget *parent, const QString &title, const QString &path);
    void reload();

    QPlainTextEdit *m_text;
    QString m_path;
};
