#include "core/selinux.h"

#include "core/backups.h"
#include "core/config.h"
#include "core/smbconf.h"
#include "core/sysutils.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <pwd.h>

namespace sm {

namespace {

const QStringList kProtectedTrees = {
    "/etc", "/usr", "/boot", "/root", "/dev", "/proc", "/sys", "/run",
    "/bin", "/sbin", "/lib", "/lib64", "/var", "/tmp",
};
const QStringList kSensitiveDirs = {".ssh", ".gnupg", ".pki"};

bool within(const QString &path, const QString &root)
{
    return path == root || path.startsWith(root + '/');
}

QSet<QString> homeDirs()
{
    QSet<QString> homes;
    ::setpwent();
    while (const passwd *e = ::getpwent()) {
        const QString dir = QString::fromLocal8Bit(e->pw_dir);
        if (e->pw_uid >= 1000 && e->pw_uid != 65534 && !dir.isEmpty() && dir != QLatin1String("/"))
            homes.insert(realPath(dir));
    }
    ::endpwent();
    return homes;
}

//! Escapa só os metacaracteres de regex (semanage fcontext usa regex).
QString regexEscape(const QString &s)
{
    static const QString meta = QStringLiteral(".^$*+?()[]{}|\\");
    QString out;
    for (const QChar c : s) {
        if (meta.contains(c))
            out += '\\';
        out += c;
    }
    return out;
}

}  // namespace

QString labelBlockReason(const QString &path)
{
    const QString real = realPath(path);
    if (config::systemPaths().contains(real))
        return QStringLiteral("pasta do sistema, não será rotulada (compartilhe uma subpasta).");
    const QSet<QString> homes = homeDirs();
    if (homes.contains(real))
        return QStringLiteral("é uma pasta pessoal inteira. Rotulá-la trocaria o contexto de "
                              "~/.ssh e de outros arquivos. Compartilhe uma subpasta ou use "
                              "“SELinux: pastas pessoais” na tela Serviço.");
    const QStringList parts = real.split('/');
    for (const QString &d : kSensitiveDirs)
        if (parts.contains(d) || QFileInfo(QDir(real).filePath(d)).isDir())
            return QStringLiteral("contém ou fica dentro de %1, cujo rótulo SELinux precisa ser "
                                  "preservado. Compartilhe uma subpasta específica.").arg(d);
    for (const QString &h : homes)
        if (within(real, h))
            return {};  // subpasta comum de uma home (inclusive /var/home no Silverblue)
    for (const QString &tree : kProtectedTrees)
        if (within(real, tree))
            return QStringLiteral("fica dentro de %1 (área do sistema), não será rotulada.").arg(tree);
    return {};
}

QPair<bool, QString> labelPath(const QString &path)
{
    const QString reason = labelBlockReason(path);
    if (!reason.isEmpty())
        return {false, path + QStringLiteral(": ") + reason};
    const QString real = realPath(path);
    if (!QFileInfo(real).isDir())
        return {false, path + QStringLiteral(": a pasta não existe.")};
    if (!which(QStringLiteral("semanage")).isEmpty()) {
        const QString spec = regexEscape(real) + QStringLiteral("(/.*)?");
        RunResult r = run({"semanage", "fcontext", "-a", "-t", "samba_share_t", spec});
        if (r.rc != 0)
            r = run({"semanage", "fcontext", "-m", "-t", "samba_share_t", spec});
        if (r.rc != 0)
            return {false, path + QStringLiteral(": semanage falhou: ") + r.out};
        r = run({"restorecon", "-R", real});
        if (r.rc != 0)
            return {false, path + QStringLiteral(": restorecon falhou: ") + r.out};
        return {true, path + QStringLiteral(": samba_share_t aplicado (permanente)")};
    }
    const RunResult r = run({"chcon", "-R", "-t", "samba_share_t", real});
    if (r.rc != 0)
        return {false, path + QStringLiteral(": chcon falhou: ") + r.out};
    return {true, path + QStringLiteral(": samba_share_t aplicado (temporário; instale "
                                        "policycoreutils-python-utils)")};
}

QList<QPair<bool, QString>> relabelAllShares()
{
    QStringList paths;
    for (const auto &[name, params] :
         SmbConf::load().shares({QStringLiteral("global"), QStringLiteral("printers"), QStringLiteral("print$")})) {
        const QString p = params.value(QStringLiteral("path")).trimmed();
        if (!p.isEmpty() && !paths.contains(QDir::cleanPath(p)))
            paths << QDir::cleanPath(p);
    }
    QList<QPair<bool, QString>> results;
    QStringList msgs;
    for (const QString &p : paths) {
        results << labelPath(p);
        msgs << results.last().second;
    }
    if (!results.isEmpty())
        logChange(config::smbConf(), QStringLiteral("Reaplicar rótulos SELinux"), msgs.join('\n'));
    return results;
}

}  // namespace sm
