#include "core/config.h"

#include <QDir>

namespace sm::config {

static QString env(const char *name, const char *fallback)
{
    const QByteArray v = qgetenv(name);
    return v.isEmpty() ? QString::fromUtf8(fallback) : QString::fromLocal8Bit(v);
}

QString smbConf() { return env("SM_SMB_CONF", "/etc/samba/smb.conf"); }
QString passdb() { return env("SM_PASSDB", "/var/lib/samba/private/passdb.tdb"); }
QString appDir() { return env("SM_APP_DIR", "/var/lib/samba-manager-ng"); }
QString backupRoot() { return QDir(appDir()).filePath(QStringLiteral("backups")); }

QList<QPair<QString, QString>> tracked()
{
    return {
        {smbConf(), QStringLiteral("Configuração do Samba")},
        {passdb(), QStringLiteral("Banco de usuários Samba (binário)")},
        {QStringLiteral("/etc/passwd"), QStringLiteral("Usuários do sistema")},
        {QStringLiteral("/etc/group"), QStringLiteral("Grupos do sistema")},
    };
}

bool isTracked(const QString &path)
{
    for (const auto &[p, _] : tracked())
        if (p == path)
            return true;
    return false;
}

bool isViewOnly(const QString &path)
{
    return path == QLatin1String("/etc/passwd") || path == QLatin1String("/etc/group");
}

const QSet<QString> &systemPaths()
{
    static const QSet<QString> paths = {
        "/", "/etc", "/usr", "/var", "/boot", "/root", "/home", "/bin", "/sbin",
        "/lib", "/lib64", "/dev", "/proc", "/sys", "/run", "/tmp",
    };
    return paths;
}

}  // namespace sm::config
