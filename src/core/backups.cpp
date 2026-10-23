#include "core/backups.h"

#include "core/config.h"
#include "core/sysutils.h"
#include "core/textdiff.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cerrno>
#include <exception>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sm {

namespace {

QString indexPath(const QString &dir) { return QDir(dir).filePath(QStringLiteral("index.json")); }

QJsonObject loadIndex(const QString &dir)
{
    QFile f(indexPath(dir));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void saveIndex(const QString &dir, const QJsonObject &index)
{
    QSaveFile f(indexPath(dir));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(index).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

bool writeExclusive(const QString &path, const QByteArray &data, int flags)
{
    const int fd = ::open(QFile::encodeName(path).constData(), O_WRONLY | O_CREAT | flags, 0600);
    if (fd < 0)
        return false;
    qsizetype done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.constData() + done, static_cast<size_t>(data.size() - done));
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            ::close(fd);
            return false;
        }
        done += n;
    }
    ::fsync(fd);
    ::close(fd);
    return true;
}

FileState passdbState()
{
    const QString passdb = config::passdb();
    if (!QFile::exists(passdb))
        return {};
    FileState st;
    QTemporaryDir tmp;
    if (tmp.isValid()) {
        const QString dest = tmp.filePath(QStringLiteral("passdb.tdb"));
        if (run({"pdbedit", "-e", "tdbsam:" + dest}).rc == 0)
            st.data = readBytes(dest);
    }
    if (!st.data)
        st.data = readBytes(passdb);  // sem export: cópia direta (melhor esforço)
    const RunResult r = run({"pdbedit", "-L", "-w"});
    if (r.rc == 0)
        st.entries = parseSmbpasswd(r.out);
    return st;
}

}  // namespace

QString fileDir(const QString &path, bool create)
{
    QString slug = path;
    while (slug.startsWith('/'))
        slug.remove(0, 1);
    while (slug.endsWith('/'))
        slug.chop(1);
    slug.replace('/', QStringLiteral("__"));
    const QString dir = QDir(config::backupRoot()).filePath(slug);
    if (create) {
        QDir().mkpath(dir);
        ::chmod(QFile::encodeName(config::appDir()).constData(), 0700);
        ::chmod(QFile::encodeName(config::backupRoot()).constData(), 0700);
        ::chmod(QFile::encodeName(dir).constData(), 0700);
    }
    return dir;
}

QString backupFile(const QString &path, const QString &reason, const std::optional<QByteArray> &data)
{
    const std::optional<QByteArray> bytes = data ? data : captureState(path).data;
    if (!bytes)
        return {};
    const QString dir = fileDir(path, true);
    const QDateTime now = QDateTime::currentDateTime();
    const QString name = now.toString(QStringLiteral("yyyyMMdd-HHmmss-")) +
                         QStringLiteral("%1.bak").arg(now.time().msec() * 1000, 6, 10, QLatin1Char('0'));
    QString finalName = name;
    for (int i = 1; !writeExclusive(QDir(dir).filePath(finalName), *bytes, O_EXCL); ++i) {
        if (errno != EEXIST || i > 999)
            return {};
        // Dois backups no mesmo milissegundo: avança o sufixo numérico.
        const qint64 us = finalName.mid(16, 6).toLongLong() + 1;
        finalName = name.left(16) + QStringLiteral("%1.bak").arg(us, 6, 10, QLatin1Char('0'));
    }
    QJsonObject index = loadIndex(dir);
    index[finalName] = QJsonObject{{"reason", reason}, {"size", static_cast<qint64>(bytes->size())}};
    QStringList versions = QDir(dir).entryList({QStringLiteral("*.bak")}, QDir::Files, QDir::Name);
    while (versions.size() > config::kMaxVersions) {
        const QString old = versions.takeFirst();
        QFile::remove(QDir(dir).filePath(old));
        index.remove(old);
    }
    saveIndex(dir, index);
    return finalName;
}

QList<Version> listVersions(const QString &path)
{
    const QString dir = fileDir(path, false);
    QStringList names = QDir(dir).entryList({QStringLiteral("*.bak")}, QDir::Files, QDir::Name);
    std::reverse(names.begin(), names.end());
    const QJsonObject index = loadIndex(dir);
    QList<Version> out;
    for (const QString &n : names) {
        const QJsonObject meta = index.value(n).toObject();
        Version v{n, meta.value(QStringLiteral("reason")).toString(QStringLiteral("?")),
                  meta.contains(QStringLiteral("size"))
                      ? meta.value(QStringLiteral("size")).toInteger()
                      : QFileInfo(QDir(dir).filePath(n)).size()};
        out.append(v);
    }
    return out;
}

bool isValidVersionName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral(R"(^\d{8}-\d{6}-\d{6}\.bak$)"));
    return re.match(name).hasMatch();
}

std::optional<QByteArray> readVersion(const QString &path, const QString &name)
{
    if (!isValidVersionName(name))  // vem do auxiliar: nada de "../"
        return std::nullopt;
    return readBytes(QDir(fileDir(path, false)).filePath(name));
}

QString versionLabel(const QString &name)
{
    const QDateTime dt = QDateTime::fromString(name.left(15), QStringLiteral("yyyyMMdd-HHmmss"));
    return dt.isValid() ? dt.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")) : name;
}

void logChange(const QString &path, const QString &action, const QString &detail)
{
    const QString dir = fileDir(path, true);
    QStringList lines{QStringLiteral("[%1] %2").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), action)};
    for (const QString &ln : detail.split('\n', Qt::SkipEmptyParts))
        lines << QStringLiteral("    ") + ln;
    writeExclusive(QDir(dir).filePath(QStringLiteral("changes.log")),
                   (lines.join('\n') + QStringLiteral("\n\n")).toUtf8(), O_APPEND);
}

QString readLog(const QString &path)
{
    const auto data = readBytes(QDir(fileDir(path, false)).filePath(QStringLiteral("changes.log")));
    return data ? QString::fromUtf8(*data) : QString();
}

QString describeChange(const QString &path, const std::optional<QByteArray> &before,
                       const std::optional<QByteArray> &after)
{
    if (!before)
        return QStringLiteral("Arquivo criado.");
    if (!after)
        return QStringLiteral("Arquivo removido.");
    if (isBinary(path, *before) || isBinary(path, *after)) {
        auto h = [](const QByteArray &b) {
            return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex().left(12));
        };
        return QStringLiteral("Arquivo binário alterado: %1 -> %2 bytes (sha256 %3 -> %4)")
            .arg(before->size()).arg(after->size()).arg(h(*before), h(*after));
    }
    auto lines = [](const QByteArray &b) {
        QStringList l = QString::fromUtf8(b).split('\n');
        if (!l.isEmpty() && l.last().isEmpty())
            l.removeLast();
        return l;
    };
    QStringList diff;
    for (const QString &ln : unifiedDiff(lines(*before), lines(*after), {}, {}, 0))
        if (!ln.startsWith(QLatin1String("---")) && !ln.startsWith(QLatin1String("+++")) &&
            !ln.startsWith(QLatin1String("@@")))
            diff << ln;
    if (diff.size() > config::kMaxDiffLines) {
        const auto omitted = diff.size() - config::kMaxDiffLines;
        diff = diff.mid(0, config::kMaxDiffLines);
        diff << QStringLiteral("... (%1 linhas omitidas)").arg(omitted);
    }
    return diff.isEmpty() ? QStringLiteral("Alteração sem diferenças textuais.") : diff.join('\n');
}

PassdbEntries parseSmbpasswd(const QString &text)
{
    PassdbEntries out;
    for (const QString &line : text.split('\n')) {
        const QStringList f = line.split(':');
        if (f.size() >= 6) {
            QString flags = f[4];
            flags.remove('[').remove(']');
            out[f[0]] = {flags.trimmed(), f[3], f[5]};
        }
    }
    return out;
}

QString describePassdbChange(const PassdbEntries &before, const PassdbEntries &after)
{
    QStringList lines;
    for (auto it = after.cbegin(); it != after.cend(); ++it)
        if (!before.contains(it.key()))
            lines << QStringLiteral("Usuário adicionado: ") + it.key();
    for (auto it = before.cbegin(); it != before.cend(); ++it)
        if (!after.contains(it.key()))
            lines << QStringLiteral("Usuário removido: ") + it.key();
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        if (!after.contains(it.key()))
            continue;
        const PassdbEntry &a = it.value(), &b = after[it.key()];
        const bool wasOff = a.flags.contains('D'), isOff = b.flags.contains('D');
        if (wasOff && !isOff)
            lines << QStringLiteral("Usuário habilitado: ") + it.key();
        else if (!wasOff && isOff)
            lines << QStringLiteral("Usuário desabilitado: ") + it.key();
        else if (a.flags != b.flags)
            lines << QStringLiteral("Flags de %1: [%2] -> [%3]").arg(it.key(), a.flags, b.flags);
        if (a.ntHash != b.ntHash)
            lines << QStringLiteral("Senha alterada: ") + it.key();
    }
    return lines.isEmpty() ? QStringLiteral("Alteração interna no banco (sem mudança de contas).")
                           : lines.join('\n');
}

bool FileState::sameAs(const FileState &o) const
{
    if (entries && o.entries)
        return *entries == *o.entries;
    return data == o.data;
}

FileState captureState(const QString &path)
{
    if (path == config::passdb())
        return passdbState();
    return {readBytes(path), std::nullopt};
}

// ---- Tracker ----

Tracker::Tracker(QString action, QStringList paths, QStringList notes)
    : m_action(std::move(action)), m_paths(std::move(paths)), m_notes(std::move(notes)),
      m_uncaught(std::uncaught_exceptions())
{
    if (m_paths.isEmpty())
        for (const auto &[p, _] : config::tracked())
            m_paths << p;
    for (const QString &p : m_paths)
        m_before << captureState(p);
}

Tracker::~Tracker()
{
    try {
        const bool interrupted = std::uncaught_exceptions() > m_uncaught;
        const QString action = m_action + (interrupted ? QStringLiteral(" (interrompido por erro)") : QString());
        const QString suffix = m_notes.isEmpty() ? QString() : '\n' + m_notes.join('\n');
        for (qsizetype i = 0; i < m_paths.size(); ++i) {
            const QString &p = m_paths[i];
            const FileState &before = m_before[i];
            const FileState after = captureState(p);
            if (before.sameAs(after))
                continue;
            if (before.data)
                backupFile(p, m_action, before.data);
            const QString detail = before.entries && after.entries
                                       ? describePassdbChange(*before.entries, *after.entries)
                                       : describeChange(p, before.data, after.data);
            logChange(p, action, detail + suffix);
        }
    } catch (...) {
        // Destrutor não pode propagar: falha no registro não desfaz a operação.
    }
}

}  // namespace sm
