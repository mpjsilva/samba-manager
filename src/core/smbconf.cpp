#include "core/smbconf.h"

#include "core/backups.h"
#include "core/config.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryFile>

#include <optional>
#include <stdexcept>

namespace sm {

namespace {

const QRegularExpression &sectionRe()
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*\[(.+?)\]\s*$)"));
    return re;
}

//! Diretivas que podem aparecer várias vezes (cada linha conta).
bool isMultiKey(const QString &key) { return key == QLatin1String("include"); }

struct Logical {
    qsizetype start, end;  // [start, end) nas linhas da seção
    QString text;
};

//! Junta linhas continuadas com "\".
QList<Logical> logicalLines(const QStringList &lines)
{
    QList<Logical> out;
    qsizetype i = 0;
    while (i < lines.size()) {
        const qsizetype start = i;
        QString text = lines[i];
        while (text.trimmed().endsWith('\\') && i + 1 < lines.size()) {
            ++i;
            QString head = text.trimmed();
            head.chop(1);
            text = head.trimmed() + ' ' + lines[i].trimmed();
        }
        out.append({start, i + 1, text});
        ++i;
    }
    return out;
}

std::optional<QPair<QString, QString>> param(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || t[0] == '#' || t[0] == ';' || !t.contains('='))
        return std::nullopt;
    const qsizetype eq = t.indexOf('=');
    return QPair<QString, QString>{t.left(eq).trimmed().toLower(), t.mid(eq + 1).trimmed()};
}

Params parseParams(const QStringList &body)
{
    Params params;
    for (const Logical &l : logicalLines(body))
        if (const auto p = param(l.text))
            params.set(p->first, p->second);  // repetido: vale o último, como no Samba
    return params;
}

QString leadingWhitespace(const QString &s)
{
    qsizetype i = 0;
    while (i < s.size() && s[i].isSpace())
        ++i;
    return s.left(i);
}

}  // namespace

// ---- Params ----

void Params::set(const QString &key, const QString &value)
{
    for (auto &kv : m_items)
        if (kv.first == key) {
            kv.second = value;
            return;
        }
    m_items.append({key, value});
}

QString Params::value(const QString &key, const QString &fallback) const
{
    for (const auto &kv : m_items)
        if (kv.first == key)
            return kv.second;
    return fallback;
}

bool Params::contains(const QString &key) const
{
    for (const auto &kv : m_items)
        if (kv.first == key)
            return true;
    return false;
}

void Params::remove(const QString &key)
{
    m_items.removeIf([&](const auto &kv) { return kv.first == key; });
}

// ---- SmbConf ----

SmbConf::SmbConf(const QString &text)
{
    m_sections.append({QString(), {}});
    if (text.isEmpty())
        return;
    QStringList lines = text.split('\n');
    if (text.endsWith('\n'))
        lines.removeLast();
    for (const QString &line : lines) {
        const auto m = sectionRe().match(line);
        if (m.hasMatch())
            m_sections.append({m.captured(1), {line}});
        else
            m_sections.last().lines.append(line);
    }
}

SmbConf SmbConf::load()
{
    if (const auto data = readBytes(config::smbConf()))
        return SmbConf(QString::fromUtf8(*data));
    return SmbConf(QStringLiteral("[global]\n\tworkgroup = WORKGROUP\n"));
}

const SmbConf::Section *SmbConf::find(const QString &name) const
{
    for (const Section &s : m_sections)
        if (!s.name.isEmpty() && s.name.compare(name, Qt::CaseInsensitive) == 0)
            return &s;
    return nullptr;
}

SmbConf::Section *SmbConf::find(const QString &name)
{
    return const_cast<Section *>(std::as_const(*this).find(name));
}

QStringList SmbConf::names() const
{
    QStringList out;
    for (const Section &s : m_sections)
        if (!s.name.isEmpty())
            out << s.name;
    return out;
}

bool SmbConf::has(const QString &name) const { return find(name) != nullptr; }

Params SmbConf::get(const QString &name) const
{
    const Section *s = find(name);
    return s ? parseParams(s->lines.mid(1)) : Params();
}

QList<QPair<QString, Params>> SmbConf::shares(const QStringList &skip) const
{
    QList<QPair<QString, Params>> out;
    for (const Section &s : m_sections)
        if (!s.name.isEmpty() && !skip.contains(s.name.toLower()))
            out.append({s.name, parseParams(s.lines.mid(1))});
    return out;
}

void SmbConf::set(const QString &name, const Params &params)
{
    QStringList texts{name};
    for (const auto &[k, v] : params.items())
        texts << k << v;
    for (const QString &t : texts)
        if (hasControlChars(t))
            throw std::invalid_argument(
                ("Valor inválido para o smb.conf: " + t.left(80)).toStdString());
    if (name.contains(']'))
        throw std::invalid_argument(("Nome de seção inválido: " + name).toStdString());

    Params wanted;
    for (const auto &[k, v] : params.items())
        if (!v.isEmpty())
            wanted.set(k.trimmed().toLower(), v);

    Section *sec = find(name);
    if (!sec) {
        QStringList lines{QStringLiteral("[%1]").arg(name)};
        for (const auto &[k, v] : wanted.items())
            lines << QStringLiteral("\t%1 = %2").arg(k, v);
        lines << QString();
        m_sections.append({name, lines});
        return;
    }

    const QStringList body = sec->lines.mid(1);
    QStringList newBody;
    QSet<QString> done;
    QSet<QPair<QString, QString>> multiSeen;
    qsizetype afterLastParam = -1;
    for (const Logical &l : logicalLines(body)) {
        const auto p = param(l.text);
        if (!p) {
            newBody << body.mid(l.start, l.end - l.start);  // comentário/branco: intocado
            continue;
        }
        const auto &[key, old] = *p;
        if (isMultiKey(key)) {
            newBody << body.mid(l.start, l.end - l.start);
            multiSeen.insert({key, old});
            afterLastParam = newBody.size();
            continue;
        }
        if (!wanted.contains(key) || done.contains(key))
            continue;  // removido, ou repetição (vale só uma linha)
        if (old == wanted.value(key)) {
            newBody << body.mid(l.start, l.end - l.start);
        } else {
            QString indent = leadingWhitespace(body[l.start]);
            if (indent.isEmpty())
                indent = QStringLiteral("\t");
            const QString origKey = body[l.start].section('=', 0, 0).trimmed();
            newBody << QStringLiteral("%1%2 = %3").arg(indent, origKey, wanted.value(key));
        }
        done.insert(key);
        afterLastParam = newBody.size();
    }
    QStringList newLines;
    for (const auto &[k, v] : wanted.items())
        if (!done.contains(k) && !multiSeen.contains({k, v}))
            newLines << QStringLiteral("\t%1 = %2").arg(k, v);
    if (afterLastParam < 0) {
        // Sem parâmetros restantes: novos entram antes das linhas em branco finais.
        afterLastParam = newBody.size();
        while (afterLastParam > 0 && newBody[afterLastParam - 1].trimmed().isEmpty())
            --afterLastParam;
    }
    for (qsizetype i = 0; i < newLines.size(); ++i)
        newBody.insert(afterLastParam + i, newLines[i]);
    const QString header = sec->name == name ? sec->lines.first() : QStringLiteral("[%1]").arg(name);
    sec->name = name;
    sec->lines = QStringList{header} + newBody;
}

void SmbConf::remove(const QString &name)
{
    m_sections.removeIf([&](const Section &s) {
        return !s.name.isEmpty() && s.name.compare(name, Qt::CaseInsensitive) == 0;
    });
}

QString SmbConf::dump() const
{
    QStringList out;
    for (const Section &s : m_sections)
        out << s.lines;
    QString text = out.join('\n');
    while (text.endsWith('\n') || text.endsWith(' ') || text.endsWith('\t'))
        text.chop(1);
    return text + '\n';
}

// ---- validação ----

bool isReservedSection(const QString &name)
{
    static const QSet<QString> reserved = {"global", "homes", "printers", "print$"};
    return reserved.contains(name.toLower());
}

bool hasControlChars(const QString &text)
{
    for (const QChar c : text)
        if ((c.unicode() < 0x20 && c != '\t') || c.unicode() == 0x7f)
            return true;
    return false;
}

QString shareNameError(const QString &name, const QStringList &existing, bool creating)
{
    static const QRegularExpression re(QStringLiteral(R"(^[\w .-]+$)"),
                                       QRegularExpression::UseUnicodePropertiesOption);
    if (!re.match(name).hasMatch())
        return QStringLiteral("Use letras, números, espaço, ponto, hífen ou sublinhado.");
    if (isReservedSection(name))
        return QStringLiteral("“%1” é um nome reservado do Samba.").arg(name);
    const bool taken = existing.contains(name, Qt::CaseInsensitive);
    if (creating && taken)
        return QStringLiteral("Já existe um compartilhamento chamado “%1”.").arg(name);
    if (!creating && !taken)
        return QStringLiteral("O compartilhamento “%1” não existe mais.").arg(name);
    return {};
}

// ---- gravação ----

RunResult testConfBytes(const QByteArray &data)
{
    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/sm-XXXXXX.conf"));
    if (!tmp.open() || tmp.write(data) != data.size() || !tmp.flush())
        return {1, QStringLiteral("Falha ao criar arquivo temporário para o testparm.")};
    return run({QStringLiteral("testparm"), QStringLiteral("-s"), tmp.fileName()});
}

QPair<bool, QString> saveConf(const SmbConf &conf, const QString &action, const QStringList &notes)
{
    const QByteArray data = conf.dump().toUtf8();
    const RunResult t = testConfBytes(data);
    if (t.rc != 0)
        return {false, QStringLiteral("Configuração inválida:\n") + t.out};
    try {
        Tracker tracker(action, {config::smbConf()}, notes);
        writeFile(config::smbConf(), data);
    } catch (const std::exception &e) {
        return {false, QStringLiteral("Falha ao gravar %1:\n%2")
                           .arg(config::smbConf(), QString::fromUtf8(e.what()))};
    }
    run({QStringLiteral("smbcontrol"), QStringLiteral("all"), QStringLiteral("reload-config")});
    return {true, QStringLiteral("Configuração salva e recarregada.")};
}

}  // namespace sm
