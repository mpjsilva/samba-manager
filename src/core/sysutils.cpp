#include "core/sysutils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <pwd.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace sm {

static std::runtime_error sysError(const QString &what)
{
    return std::runtime_error(
        (what + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(errno))).toStdString());
}

RunResult run(const QStringList &cmd, const std::optional<QString> &input, int timeoutSec)
{
    if (cmd.isEmpty())
        return {127, QStringLiteral("Comando vazio.")};
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    if (!input)
        p.setStandardInputFile(QProcess::nullDevice());
    p.start(cmd.first(), cmd.mid(1));
    if (!p.waitForStarted(10000))
        return {127, QStringLiteral("Comando não encontrado: ") + cmd.first()};
    if (input) {
        p.write(input->toUtf8());
        p.closeWriteChannel();
    }
    if (!p.waitForFinished(timeoutSec * 1000)) {
        p.kill();
        p.waitForFinished(2000);
        return {124, QStringLiteral("Tempo esgotado.")};
    }
    const QString out = QString::fromUtf8(p.readAll()).trimmed();
    if (p.exitStatus() == QProcess::CrashExit)
        return {-1, out.isEmpty() ? QStringLiteral("O comando foi encerrado por um sinal.") : out};
    return {p.exitCode(), out};
}

QString which(const QString &cmd)
{
    return QStandardPaths::findExecutable(cmd);
}

QMap<QString, QPair<QString, QString>> serviceStates(const QStringList &names)
{
    QMap<QString, QPair<QString, QString>> parsed;
    if (names.isEmpty())
        return parsed;
    const RunResult r = run(QStringList{"systemctl", "show"} + names +
                            QStringList{"--property=Id,ActiveState,UnitFileState"});
    if (r.rc == 0) {
        QString id, active, enabled;
        auto flush = [&] {
            if (!id.isEmpty()) {
                QString base = id;
                if (base.endsWith(QLatin1String(".service")))
                    base.chop(8);
                parsed[base] = {active.isEmpty() ? "?" : active, enabled.isEmpty() ? "?" : enabled};
            }
            id.clear(), active.clear(), enabled.clear();
        };
        for (const QString &line : r.out.split('\n')) {
            if (line.trimmed().isEmpty())
                flush();
            else if (line.startsWith(QLatin1String("Id=")))
                id = line.mid(3).trimmed();
            else if (line.startsWith(QLatin1String("ActiveState=")))
                active = line.mid(12).trimmed();
            else if (line.startsWith(QLatin1String("UnitFileState=")))
                enabled = line.mid(14).trimmed();
        }
        flush();
    }
    QMap<QString, QPair<QString, QString>> out;
    for (const QString &n : names)
        out[n] = parsed.value(n, {QStringLiteral("?"), QStringLiteral("?")});
    return out;
}

std::optional<QByteArray> readBytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return f.readAll();
}

void writeFile(const QString &path, const QByteArray &data)
{
    const QFileInfo info(path);
    const QString parent = info.absolutePath();
    QDir().mkpath(parent);

    struct stat st {};
    const bool existed = ::stat(QFile::encodeName(path).constData(), &st) == 0;

    QByteArray tmpl = QFile::encodeName(parent + QStringLiteral("/.sm-tmp-XXXXXX"));
    const int fd = ::mkstemp(tmpl.data());
    if (fd < 0)
        throw sysError(QStringLiteral("Falha ao criar temporário em ") + parent);
    auto fail = [&](const QString &what) {
        const auto err = sysError(what);
        ::close(fd);
        ::unlink(tmpl.constData());
        return err;
    };
    qsizetype done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.constData() + done, static_cast<size_t>(data.size() - done));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            throw fail(QStringLiteral("Falha ao gravar ") + path);
        }
        done += n;
    }
    ::fsync(fd);
    if (existed) {
        if (::fchown(fd, st.st_uid, st.st_gid) != 0 && errno != EPERM)
            throw fail(QStringLiteral("Falha ao ajustar dono de ") + path);
        ::fchmod(fd, st.st_mode & 07777);
    }
    ::close(fd);
    if (::rename(tmpl.constData(), QFile::encodeName(path).constData()) != 0) {
        const auto err = sysError(QStringLiteral("Falha ao substituir ") + path);
        ::unlink(tmpl.constData());
        throw err;
    }
    if (!which(QStringLiteral("restorecon")).isEmpty())
        run({QStringLiteral("restorecon"), path});
}

bool isBinary(const QString &path, const QByteArray &data)
{
    return path.endsWith(QLatin1String(".tdb")) || data.left(8192).contains('\0');
}

std::optional<Owner> invokingUser()
{
    for (const char *var : {"PKEXEC_UID", "SUDO_UID"}) {
        bool ok = false;
        const uint uid = qEnvironmentVariable(var).toUInt(&ok);
        if (!ok || uid == 0)
            continue;
        if (const passwd *pw = ::getpwuid(static_cast<uid_t>(uid)))
            return Owner{pw->pw_uid, pw->pw_gid};
    }
    return std::nullopt;
}

QString userName(uid_t uid)
{
    const passwd *pw = ::getpwuid(uid);
    return pw ? QString::fromLocal8Bit(pw->pw_name) : QString::number(uid);
}

QStringList makeShareDir(const QString &path, const std::optional<Owner> &owner)
{
    QStringList missing;  // filho -> pai
    QString p = QDir::cleanPath(QDir(path).absolutePath());
    while (!QFileInfo::exists(p)) {
        missing << p;
        p = QFileInfo(p).path();
    }
    if (missing.isEmpty())
        return {};
    for (auto it = missing.crbegin(); it != missing.crend(); ++it)
        if (::mkdir(QFile::encodeName(*it).constData(), 0755) != 0 && errno != EEXIST)
            throw sysError(QStringLiteral("Falha ao criar ") + *it);
    const QByteArray leaf = QFile::encodeName(missing.first());
    if (owner && ::chown(leaf.constData(), owner->uid, owner->gid) != 0)
        throw sysError(QStringLiteral("Falha ao ajustar dono de ") + missing.first());
    if (::chmod(leaf.constData(), 02775) != 0)
        throw sysError(QStringLiteral("Falha ao ajustar modo de ") + missing.first());
    std::reverse(missing.begin(), missing.end());
    return missing;
}

QString realPath(const QString &path)
{
    QString existing = QDir::cleanPath(QDir(path).absolutePath());
    QStringList rest;
    while (!QFileInfo::exists(existing) && existing != QLatin1String("/")) {
        rest.prepend(QFileInfo(existing).fileName());
        existing = QFileInfo(existing).path();
    }
    const QString canon = QFileInfo(existing).canonicalFilePath();
    QString out = canon.isEmpty() ? existing : canon;
    for (const QString &part : rest)
        out = QDir::cleanPath(out + '/' + part);
    return out;
}

}  // namespace sm
