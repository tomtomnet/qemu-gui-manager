// SPDX-License-Identifier: GPL-2.0-or-later
#include "qemuinfo.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrent>

/* The column where -help puts the descriptions */
static const int kHelpColumn = 16;

const QemuOptionDoc *QemuInfo::option(const QString &name) const
{
    for (const QemuOptionDoc &o : options) {
        if (o.name == name) {
            return &o;
        }
    }
    return nullptr;
}

const QemuDeviceDoc *QemuInfo::device(const QString &name) const
{
    for (const QemuDeviceDoc &d : devices) {
        if (d.name == name || d.aliases.contains(name)) {
            return &d;
        }
    }
    return nullptr;
}

QList<QemuOptionDoc> QemuInfo::parseHelp(const QString &text)
{
    static const QRegularExpression gap("\\s{2,}");
    QList<QemuOptionDoc> out;
    QString section;
    qsizetype current = -1;     /* first entry of the option being read */

    for (const QString &line : text.split('\n')) {
        if (line.trimmed().isEmpty()) {
            current = -1;
            continue;
        }
        if (line.startsWith('-')) {
            QemuOptionDoc doc;
            QRegularExpressionMatch m = gap.match(line, 1);
            qsizetype split = -1;

            /* the description starts at the help column, if on this line */
            while (m.hasMatch()) {
                if (m.capturedEnd() >= kHelpColumn) {
                    split = m.capturedStart();
                    break;
                }
                m = gap.match(line, m.capturedEnd());
            }
            doc.synopsis = (split < 0 ? line : line.left(split)).trimmed();
            doc.help = split < 0 ? QString() : line.mid(m.capturedEnd()).trimmed();
            doc.section = section;

            const QStringList words = doc.synopsis.split(' ', Qt::SkipEmptyParts);
            doc.takesValue = words.size() > 1 && words[1] != "or";
            current = out.size();
            /* -hda/-hdb file: one entry each */
            for (const QString &name : words[0].mid(1).split("/-")) {
                doc.name = name;
                out.append(doc);
            }
        } else if (line.startsWith(' ')) {
            for (qsizetype i = current; i >= 0 && i < out.size(); i++) {
                QString &help = out[i].help;
                help += (help.isEmpty() ? "" : "\n") + line.trimmed();
            }
        } else {
            /* e.g. "Standard options:" */
            current = -1;
            if (line.trimmed().endsWith(':')) {
                section = line.trimmed().chopped(1);
            }
        }
    }

    /* -M as -machine */
    for (QemuOptionDoc &o : out) {
        if (o.help.startsWith("as -")) {
            const QString target = o.help.mid(4).section(' ', 0, 0);
            for (const QemuOptionDoc &t : out) {
                if (t.name == target) {
                    o.takesValue = t.takesValue;
                }
            }
        }
    }
    return out;
}

/* The C string literals in @text, concatenated per literal */
static QStringList stringLiterals(const QString &text)
{
    QStringList literals;

    for (qsizetype i = 0; i < text.size(); i++) {
        if (text[i] != '"') {
            continue;
        }
        QString lit;
        for (i++; i < text.size() && text[i] != '"'; i++) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                QChar c = text[++i];
                lit += c == 'n' ? QChar('\n') : c == 't' ? QChar('\t') : c;
            } else {
                lit += text[i];
            }
        }
        literals << lit;
    }
    return literals;
}

void QemuInfo::mergeOptionsHx(QList<QemuOptionDoc> &options, const QString &hx)
{
    struct Def {
        QString name;
        bool takesValue;
        QString section;
    };
    QList<Def> pending;
    QString section, def, rst;
    bool inDef = false, inRst = false;
    int depth = 0;

    auto attach = [&]() {
        for (const Def &d : pending) {
            for (QemuOptionDoc &o : options) {
                if (o.name == d.name) {
                    o.details = rst;
                    o.takesValue = d.takesValue;
                    if (!d.section.isEmpty()) {
                        o.section = d.section;
                    }
                }
            }
        }
        pending.clear();
    };

    for (const QString &line : hx.split('\n')) {
        if (inRst) {
            if (line.trimmed() == "ERST") {
                inRst = false;
                attach();
            } else {
                rst += line + '\n';
            }
            continue;
        }
        if (!inDef && line.startsWith("DEFHEADING(")) {
            section = line.mid(11).section(')', 0, 0).trimmed();
            if (section.endsWith(':')) {
                section.chop(1);
            }
            continue;
        }
        if (!inDef && line.startsWith("DEF(")) {
            inDef = true;
            def.clear();
            depth = 0;
        }
        if (inDef) {
            bool inString = false;

            def += line + '\n';
            for (qsizetype i = 0; i < line.size(); i++) {
                if (line[i] == '\\' && inString) {
                    i++;
                } else if (line[i] == '"') {
                    inString = !inString;
                } else if (!inString && line[i] == '(') {
                    depth++;
                } else if (!inString && line[i] == ')') {
                    depth--;
                }
            }
            if (depth <= 0) {
                const QStringList literals = stringLiterals(def);
                if (!literals.isEmpty()) {
                    const QString afterName = def.section(',', 1, 1).trimmed();
                    pending << Def{literals[0], afterName == "HAS_ARG", section};
                }
                inDef = false;
            }
            continue;
        }
        if (line.trimmed() == "SRST") {
            inRst = true;
            rst.clear();
        }
    }
}

/* Split "a "x, y", b c" at ", " outside quotes */
static QStringList splitFields(const QString &line)
{
    QStringList fields;
    QString cur;
    bool quoted = false;

    for (qsizetype i = 0; i < line.size(); i++) {
        if (line[i] == '"') {
            quoted = !quoted;
        }
        if (!quoted && line.mid(i, 2) == ", ") {
            fields << cur;
            cur.clear();
            i++;
            continue;
        }
        cur += line[i];
    }
    fields << cur;
    return fields;
}

static QString unquote(const QString &text)
{
    const QString t = text.trimmed();
    return t.size() >= 2 && t.startsWith('"') && t.endsWith('"')
               ? t.mid(1, t.size() - 2) : t;
}

QList<QemuDeviceDoc> QemuInfo::parseDeviceHelp(const QString &text)
{
    QList<QemuDeviceDoc> out;
    QString category;

    for (const QString &line : text.split('\n')) {
        if (line.startsWith("name ")) {
            QemuDeviceDoc d;

            d.category = category;
            for (const QString &field : splitFields(line)) {
                const QString key = field.section(' ', 0, 0);
                const QString value = unquote(field.section(' ', 1));

                if (key == "name") {
                    d.name = value;
                } else if (key == "bus") {
                    d.bus = value;
                } else if (key == "alias") {
                    d.aliases << value;
                } else if (key == "desc") {
                    d.desc = value;
                } else if (key == "no-user") {
                    d.userCreatable = false;
                }
            }
            out << d;
        } else if (line.trimmed().endsWith(':')) {
            category = line.trimmed().chopped(1);
        }
    }
    return out;
}

QList<QemuPropertyDoc> QemuInfo::parsePropertyHelp(const QString &text)
{
    static const QRegularExpression def("\\s*\\(default: (.*)\\)\\s*$");
    QList<QemuPropertyDoc> out;

    for (const QString &line : text.split('\n')) {
        if (!line.startsWith("  ")) {
            continue;
        }
        const QString t = line.trimmed();
        const qsizetype eq = t.indexOf("=<");
        if (eq <= 0) {
            continue;
        }

        QemuPropertyDoc p;
        qsizetype i = eq + 2;
        int depth = 1;

        p.name = t.left(eq);
        for (; i < t.size() && depth > 0; i++) {
            if (t[i] == '<') {
                depth++;
            } else if (t[i] == '>' && --depth == 0) {
                break;
            }
            p.type += t[i];
        }
        QString rest = t.mid(i + 1).trimmed();
        if (rest.startsWith('-')) {
            rest = rest.mid(1).trimmed();
        }
        const QRegularExpressionMatch m = def.match(rest);
        if (m.hasMatch()) {
            p.defaultValue = m.captured(1);
            rest = rest.left(m.capturedStart()).trimmed();
        }
        p.desc = rest;
        out << p;
    }
    return out;
}

QList<QemuNamedDoc> QemuInfo::parseListHelp(const QString &text)
{
    QList<QemuNamedDoc> out;
    const QStringList lines = text.split('\n');

    /* after the header, until the first blank line */
    for (qsizetype i = 1; i < lines.size(); i++) {
        const QString t = lines[i].trimmed();
        if (t.isEmpty()) {
            break;
        }
        out << QemuNamedDoc{t.section(' ', 0, 0),
                            t.section(' ', 1).trimmed()};
    }
    return out;
}

static QString inlineRst(QString text)
{
    static const QRegularExpression literal("``(.+?)``");
    static const QRegularExpression strong("\\*\\*(.+?)\\*\\*");
    static const QRegularExpression emphasis("(^|[^`])`([^`]+)`");

    text = text.toHtmlEscaped().replace("\\ ", "");
    text.replace(literal, "<code>\\1</code>");
    text.replace(strong, "<b>\\1</b>");
    text.replace(emphasis, "\\1<i>\\2</i>");
    return text;
}

QString QemuInfo::rstToHtml(const QString &rst)
{
    QString html;
    QStringList block;
    bool literalNext = false;

    auto flush = [&]() {
        if (block.isEmpty()) {
            return;
        }
        const qsizetype indent = block[0].size() - block[0].trimmed().size();
        if (literalNext) {
            QString pre;
            for (const QString &l : block) {
                pre += l.mid(qMin(indent, l.size())) + '\n';
            }
            html += "<pre>" + pre.toHtmlEscaped() + "</pre>";
            literalNext = false;
        } else if (block[0].trimmed().startsWith("- ") ||
                   block[0].trimmed().startsWith("* ")) {
            html += "<ul>";
            QString item;
            for (const QString &l : block) {
                const QString t = l.trimmed();
                if (t.startsWith("- ") || t.startsWith("* ")) {
                    if (!item.isEmpty()) {
                        html += "<li>" + inlineRst(item) + "</li>";
                    }
                    item = t.mid(2);
                } else {
                    item += ' ' + t;
                }
            }
            html += "<li>" + inlineRst(item) + "</li></ul>";
        } else {
            QStringList words;
            for (const QString &l : block) {
                words << l.trimmed();
            }
            QString text = words.join(' ');
            if (text.endsWith("::")) {
                literalNext = true;
                text.chop(1);
            }
            if (indent == 0 && text.startsWith("``")) {
                html += "<p><b>" + inlineRst(text) + "</b></p>";
            } else {
                html += QString("<p style=\"margin-left:%1px\">")
                            .arg(indent > 4 ? 16 : 0) + inlineRst(text) + "</p>";
            }
        }
        block.clear();
    };

    for (const QString &line : rst.split('\n')) {
        if (line.trimmed().isEmpty()) {
            flush();
        } else {
            block << line;
        }
    }
    flush();
    return html;
}

/* Loader */

QemuInfoLoader::QemuInfoLoader(const QString &binary, QObject *parent)
    : QObject(parent), m_binary(binary)
{
}

QString QemuInfoLoader::findOptionsHx(const QString &binary)
{
    /* build/qemu-system-x86_64 -> qemu-options.hx of the source tree */
    const QDir dir = QFileInfo(binary).absoluteDir();
    for (const QString &candidate : {dir.filePath("../qemu-options.hx"),
                                     dir.filePath("qemu-options.hx")}) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).canonicalFilePath();
        }
    }
    return {};
}

QString QemuInfoLoader::run(const QStringList &args, QString *error) const
{
    QProcess p;

    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(m_binary, args);
    if (!p.waitForFinished(15000)) {
        if (error) {
            *error = p.errorString();
        }
        p.kill();
        return {};
    }
    return QString::fromUtf8(p.readAll());
}

QString QemuInfoLoader::cachePath() const
{
    const QFileInfo fi(m_binary);
    const QByteArray key = QCryptographicHash::hash(
        (fi.canonicalFilePath() + '|' +
         QString::number(fi.lastModified().toMSecsSinceEpoch()) + '|' +
         QString::number(fi.size())).toUtf8(),
        QCryptographicHash::Sha1).toHex();
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
           "/qemu-info-" + key + ".json";
}

static QJsonArray namedToJson(const QList<QemuNamedDoc> &list)
{
    QJsonArray a;
    for (const QemuNamedDoc &n : list) {
        a.append(QJsonArray{n.name, n.desc});
    }
    return a;
}

static QList<QemuNamedDoc> namedFromJson(const QJsonValue &v)
{
    QList<QemuNamedDoc> list;
    for (const QJsonValue &e : v.toArray()) {
        list << QemuNamedDoc{e[0].toString(), e[1].toString()};
    }
    return list;
}

void QemuInfoLoader::saveCache() const
{
    QJsonObject root;
    QJsonArray options, devices;
    QJsonObject properties;

    for (const QemuOptionDoc &o : m_info.options) {
        options.append(QJsonObject{{"name", o.name}, {"synopsis", o.synopsis},
                                   {"help", o.help}, {"details", o.details},
                                   {"section", o.section},
                                   {"value", o.takesValue}});
    }
    for (const QemuDeviceDoc &d : m_info.devices) {
        devices.append(QJsonObject{{"name", d.name}, {"bus", d.bus},
                                   {"desc", d.desc}, {"category", d.category},
                                   {"aliases", QJsonArray::fromStringList(d.aliases)},
                                   {"user", d.userCreatable}});
    }
    for (auto it = m_info.properties.begin(); it != m_info.properties.end(); ++it) {
        QJsonArray props;
        for (const QemuPropertyDoc &p : it.value()) {
            props.append(QJsonArray{p.name, p.type, p.desc, p.defaultValue});
        }
        properties[it.key()] = props;
    }
    root["version"] = m_info.version;
    root["options"] = options;
    root["devices"] = devices;
    root["properties"] = properties;
    root["machines"] = namedToJson(m_info.machines);
    root["cpus"] = namedToJson(m_info.cpus);
    root["objects"] = namedToJson(m_info.objects);
    root["netdevs"] = namedToJson(m_info.netdevs);
    root["chardevs"] = namedToJson(m_info.chardevs);
    root["audiodevs"] = namedToJson(m_info.audiodevs);
    root["displays"] = namedToJson(m_info.displays);
    root["accels"] = namedToJson(m_info.accels);

    QDir().mkpath(QFileInfo(cachePath()).absolutePath());
    QFile f(cachePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
}

bool QemuInfoLoader::loadCache()
{
    QFile f(cachePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.isEmpty()) {
        return false;
    }

    QemuInfo info;
    info.version = root["version"].toString();
    for (const QJsonValue &v : root["options"].toArray()) {
        info.options << QemuOptionDoc{v["name"].toString(), v["synopsis"].toString(),
                                      v["help"].toString(), v["details"].toString(),
                                      v["section"].toString(), v["value"].toBool()};
    }
    for (const QJsonValue &v : root["devices"].toArray()) {
        QemuDeviceDoc d;
        d.name = v["name"].toString();
        d.bus = v["bus"].toString();
        d.desc = v["desc"].toString();
        d.category = v["category"].toString();
        for (const QJsonValue &a : v["aliases"].toArray()) {
            d.aliases << a.toString();
        }
        d.userCreatable = v["user"].toBool(true);
        info.devices << d;
    }
    const QJsonObject props = root["properties"].toObject();
    for (auto it = props.begin(); it != props.end(); ++it) {
        QList<QemuPropertyDoc> list;
        for (const QJsonValue &p : it.value().toArray()) {
            list << QemuPropertyDoc{p[0].toString(), p[1].toString(),
                                    p[2].toString(), p[3].toString()};
        }
        info.properties[it.key()] = list;
    }
    info.machines = namedFromJson(root["machines"]);
    info.cpus = namedFromJson(root["cpus"]);
    info.objects = namedFromJson(root["objects"]);
    info.netdevs = namedFromJson(root["netdevs"]);
    info.chardevs = namedFromJson(root["chardevs"]);
    info.audiodevs = namedFromJson(root["audiodevs"]);
    info.displays = namedFromJson(root["displays"]);
    info.accels = namedFromJson(root["accels"]);
    m_info = info;
    return true;
}

void QemuInfoLoader::load()
{
    if (m_loaded) {
        emit loaded();
        return;
    }
    if (loadCache()) {
        m_loaded = true;
        emit loaded();
        return;
    }

    auto *watcher = new QFutureWatcher<QemuInfo>(this);
    connect(watcher, &QFutureWatcher<QemuInfo>::finished, this, [this, watcher]() {
        const QemuInfo info = watcher->result();
        watcher->deleteLater();
        if (info.options.isEmpty()) {
            emit failed(tr("%1 printed no options").arg(m_binary));
            return;
        }
        m_info = info;
        m_loaded = true;
        saveCache();
        emit loaded();
    });
    watcher->setFuture(QtConcurrent::run([this]() {
        QemuInfo info;
        const QString hx = findOptionsHx(m_binary);

        info.version = run({"-version"}).section('\n', 0, 0)
                           .section("version ", 1).section(' ', 0, 0).trimmed();
        info.options = QemuInfo::parseHelp(run({"-help"}));
        if (!hx.isEmpty()) {
            QFile f(hx);
            if (f.open(QIODevice::ReadOnly)) {
                QemuInfo::mergeOptionsHx(info.options, QString::fromUtf8(f.readAll()));
            }
        }
        info.devices = QemuInfo::parseDeviceHelp(run({"-device", "help"}));
        info.machines = QemuInfo::parseListHelp(run({"-machine", "help"}));
        info.cpus = QemuInfo::parseListHelp(run({"-cpu", "help"}));
        info.objects = QemuInfo::parseListHelp(run({"-object", "help"}));
        info.netdevs = QemuInfo::parseListHelp(run({"-netdev", "help"}));
        info.chardevs = QemuInfo::parseListHelp(run({"-chardev", "help"}));
        info.audiodevs = QemuInfo::parseListHelp(run({"-audiodev", "help"}));
        info.displays = QemuInfo::parseListHelp(run({"-display", "help"}));
        info.accels = QemuInfo::parseListHelp(run({"-accel", "help"}));
        return info;
    }));
}

void QemuInfoLoader::loadProperties(const QString &device)
{
    if (m_info.properties.contains(device)) {
        emit propertiesLoaded(device);
        return;
    }

    auto *watcher = new QFutureWatcher<QList<QemuPropertyDoc>>(this);
    connect(watcher, &QFutureWatcher<QList<QemuPropertyDoc>>::finished, this,
            [this, watcher, device]() {
        m_info.properties[device] = watcher->result();
        watcher->deleteLater();
        saveCache();
        emit propertiesLoaded(device);
    });
    watcher->setFuture(QtConcurrent::run([this, device]() {
        return QemuInfo::parsePropertyHelp(run({"-device", device + ",help"}));
    }));
}
