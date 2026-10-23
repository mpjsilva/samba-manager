#include "core/ops.h"

#include "core/backups.h"
#include "core/config.h"
#include "core/selinux.h"
#include "core/smbconf.h"
#include "core/sysutils.h"
#include "core/textdiff.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <stdexcept>

namespace sm {

namespace {

const QStringList kServices = {"smb", "nmb"};

[[noreturn]] void invalid(const QString &msg) { throw std::invalid_argument(msg.toStdString()); }

QString str(const QJsonObject &a, const char *key)
{
    const QJsonValue v = a.value(QLatin1String(key));
    if (!v.isString())
        invalid(QStringLiteral("Argumento ausente ou inválido: %1").arg(QLatin1String(key)));
    return v.toString();
}

bool flag(const QJsonObject &a, const char *key, std::optional<bool> fallback = std::nullopt)
{
    const QJsonValue v = a.value(QLatin1String(key));
    if (v.isUndefined() && fallback)
        return *fallback;
    if (!v.isBool())
        invalid(QStringLiteral("Argumento ausente ou inválido: %1").arg(QLatin1String(key)));
    return v.toBool();
}

QJsonArray result(const RunResult &r) { return {r.rc, r.out}; }
QJsonArray result(const QPair<bool, QString> &r) { return {r.first, r.second}; }

QString user(const QJsonObject &a)
{
    static const QRegularExpression re(QStringLiteral("^[a-z_][a-z0-9_-]{0,31}$"));
    const QString u = str(a, "user");
    if (!re.match(u).hasMatch())
        invalid(QStringLiteral("Nome de usuário inválido: %1").arg(u.left(40)));
    return u;
}

QString password(const QJsonObject &a)
{
    const QString pw = str(a, "password");
    if (pw.isEmpty() || hasControlChars(pw))
        invalid(QStringLiteral("Senha inválida (vazia ou com quebra de linha)."));
    return pw;
}

QString trackedPath(const QJsonObject &a)
{
    const QString p = str(a, "path");
    if (!config::isTracked(p))
        invalid(QStringLiteral("Arquivo não monitorado: %1").arg(p.left(80)));
    return p;
}

QString versionName(const QJsonObject &a)
{
    const QString n = str(a, "name");
    if (!isValidVersionName(n))
        invalid(QStringLiteral("Versão inválida: %1").arg(n.left(40)));
    return n;
}

//! params chega como [[chave, valor], ...] (preserva a ordem dos campos).
Params shareParams(const QJsonObject &a)
{
    static const QRegularExpression keyRe(QStringLiteral(R"(^[a-z0-9][a-z0-9 :_$-]*$)"));
    const QJsonValue v = a.value(QLatin1String("params"));
    if (!v.isArray())
        invalid(QStringLiteral("Parâmetros inválidos."));
    Params p;
    for (const QJsonValue &item : v.toArray()) {
        const QJsonArray kv = item.toArray();
        if (kv.size() != 2 || !kv[0].isString() || !kv[1].isString())
            invalid(QStringLiteral("Parâmetros inválidos."));
        const QString k = kv[0].toString(), val = kv[1].toString();
        if (!keyRe.match(k).hasMatch())
            invalid(QStringLiteral("Parâmetro inválido: %1").arg(k.left(40)));
        if (hasControlChars(val))
            invalid(QStringLiteral("Valor inválido para %1.").arg(k));
        p.set(k, val);
    }
    if (!p.value(QStringLiteral("path")).startsWith('/'))
        invalid(QStringLiteral("Informe um caminho absoluto, começando por /."));
    return p;
}

RunResult smbpasswd(const QStringList &args, const std::optional<QString> &input = std::nullopt)
{
    return run(QStringList{"smbpasswd"} + args, input);
}

OpMap build()
{
    OpMap ops;

    // ---- serviço, firewall, SELinux ----
    ops["service_status"] = [](const QJsonObject &) -> QJsonValue {
        QJsonObject out;
        const auto st = serviceStates(kServices);
        for (auto it = st.cbegin(); it != st.cend(); ++it)
            out[it.key()] = QJsonArray{it.value().first, it.value().second};
        return out;
    };
    ops["service_action"] = [](const QJsonObject &a) -> QJsonValue {
        static const QStringList allowed = {"start", "stop", "restart", "enable", "disable"};
        const QString action = str(a, "action");
        if (!allowed.contains(action))
            invalid(QStringLiteral("Ação inválida: %1").arg(action.left(40)));
        return result(run(QStringList{"systemctl", action} + kServices));
    };
    ops["firewall_open"] = [](const QJsonObject &) -> QJsonValue {
        const RunResult r1 = run({"firewall-cmd", "--permanent", "--add-service=samba"});
        const RunResult r2 = run({"firewall-cmd", "--reload"});
        return result(RunResult{r1.rc ? r1.rc : r2.rc, (r1.out + '\n' + r2.out).trimmed()});
    };
    ops["selinux_relabel"] = [](const QJsonObject &) -> QJsonValue {
        QJsonArray out;
        for (const auto &r : relabelAllShares())
            out.append(result(r));
        return out;
    };
    ops["selinux_booleans"] = [](const QJsonObject &a) -> QJsonValue {
        const QString kind = str(a, "kind");
        QStringList bools;
        if (kind == QLatin1String("homes"))
            bools = {"samba_enable_home_dirs=on"};
        else if (kind == QLatin1String("broad"))
            bools = {"samba_enable_home_dirs=on", "samba_export_all_rw=on"};
        else
            invalid(QStringLiteral("Conjunto inválido: %1").arg(kind.left(40)));
        return result(run(QStringList{"setsebool", "-P"} + bools));
    };

    // ---- smb.conf (sempre sobre o arquivo ATUAL, não sobre a cópia da UI) ----
    ops["testparm"] = [](const QJsonObject &) -> QJsonValue {
        return result(run({"testparm", "-s"}));
    };
    ops["share_save"] = [](const QJsonObject &a) -> QJsonValue {
        const Params params = shareParams(a);
        const QString name = str(a, "name");
        const bool selinux = flag(a, "selinux"), creating = flag(a, "creating");
        SmbConf conf = SmbConf::load();
        const QString err = shareNameError(name, conf.names(), creating);
        if (!err.isEmpty())
            return result(QPair<bool, QString>{false, err});
        const QString path = params.value(QStringLiteral("path"));
        QStringList notes;
        QPair<bool, QString> label{true, {}};
        if (selinux) {
            const QString reason = labelBlockReason(path);
            if (!reason.isEmpty())
                return result(QPair<bool, QString>{false, path + QStringLiteral(": ") + reason});
            const auto owner = invokingUser();
            if (!makeShareDir(path, owner).isEmpty())
                notes << QStringLiteral("Pasta criada: %1 (dono: %2, modo 2775)")
                             .arg(path, owner ? userName(owner->uid) : QStringLiteral("root"));
            label = labelPath(path);
            notes << QStringLiteral("SELinux: ") + label.second;
        }
        conf.set(name, params);
        const QString action = QStringLiteral("%1 compartilhamento [%2]")
                                   .arg(creating ? QStringLiteral("Adicionar") : QStringLiteral("Editar"), name);
        const auto saved = saveConf(conf, action, notes);
        if (saved.first && !label.first)
            return result(QPair<bool, QString>{
                false, QStringLiteral("Compartilhamento salvo, mas o rótulo SELinux falhou:\n") + label.second});
        return result(saved);
    };
    ops["share_remove"] = [](const QJsonObject &a) -> QJsonValue {
        const QString name = str(a, "name");
        SmbConf conf = SmbConf::load();
        const QString err = shareNameError(name, conf.names(), false);
        if (!err.isEmpty())
            return result(QPair<bool, QString>{false, err});
        conf.remove(name);
        return result(saveConf(conf, QStringLiteral("Remover compartilhamento [%1]").arg(name)));
    };
    ops["global_save"] = [](const QJsonObject &a) -> QJsonValue {
        static const QStringList allowed = {"workgroup", "server string", "netbios name"};
        const QJsonObject values = a.value(QLatin1String("values")).toObject();
        if (values.isEmpty())
            invalid(QStringLiteral("Campos inválidos."));
        SmbConf conf = SmbConf::load();
        Params p = conf.get(QStringLiteral("global"));
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            if (!allowed.contains(it.key()) || !it.value().isString())
                invalid(QStringLiteral("Campo inválido: %1").arg(it.key().left(40)));
            if (hasControlChars(it.value().toString()))
                invalid(QStringLiteral("Os campos não podem conter quebras de linha."));
            p.set(it.key(), it.value().toString());
        }
        conf.set(QStringLiteral("global"), p);
        return result(saveConf(conf, QStringLiteral("Editar seção [global]")));
    };

    // ---- conexões e usuários ----
    ops["smbstatus"] = [](const QJsonObject &) -> QJsonValue { return result(run({"smbstatus"})); };
    ops["users_list"] = [](const QJsonObject &) -> QJsonValue {
        return result(run({"pdbedit", "-L", "-v"}));
    };
    ops["user_add"] = [](const QJsonObject &a) -> QJsonValue {
        const QString u = user(a), pw = password(a);
        Tracker t(QStringLiteral("Adicionar usuário Samba '%1'").arg(u));
        if (run({"id", u}).rc != 0) {
            const RunResult r = run({"useradd", "-M", "-s", "/sbin/nologin", u});
            if (r.rc != 0)
                return result(r);
        }
        return result(smbpasswd({"-a", "-s", u}, pw + '\n' + pw + '\n'));
    };
    ops["user_password"] = [](const QJsonObject &a) -> QJsonValue {
        const QString u = user(a), pw = password(a);
        Tracker t(QStringLiteral("Alterar senha Samba de '%1'").arg(u));
        return result(smbpasswd({"-s", u}, pw + '\n' + pw + '\n'));
    };
    ops["user_set_enabled"] = [](const QJsonObject &a) -> QJsonValue {
        const QString u = user(a);
        const bool enabled = flag(a, "enabled");
        Tracker t(QStringLiteral("%1 usuário Samba '%2'")
                      .arg(enabled ? QStringLiteral("Habilitar") : QStringLiteral("Desabilitar"), u));
        return result(smbpasswd({enabled ? "-e" : "-d", u}));
    };
    ops["user_remove"] = [](const QJsonObject &a) -> QJsonValue {
        const QString u = user(a);
        Tracker t(QStringLiteral("Remover usuário Samba '%1'").arg(u));
        return result(smbpasswd({"-x", u}));
    };

    // ---- backups (o diretório é 0700 root: a UI não lê direto) ----
    ops["backup_list"] = [](const QJsonObject &a) -> QJsonValue {
        QJsonArray out;
        for (const Version &v : listVersions(trackedPath(a)))
            out.append(QJsonArray{v.name, QJsonObject{{"reason", v.reason}, {"size", v.size}}});
        return out;
    };
    ops["backup_log"] = [](const QJsonObject &a) -> QJsonValue { return readLog(trackedPath(a)); };
    ops["backup_view"] = [](const QJsonObject &a) -> QJsonValue {
        const QString path = trackedPath(a);
        const QByteArray data = readVersion(path, versionName(a)).value_or(QByteArray());
        if (isBinary(path, data))
            return QJsonObject{{"binary", true}, {"text", ""}};
        return QJsonObject{{"binary", false}, {"text", QString::fromUtf8(data)}};
    };
    ops["backup_diff"] = [](const QJsonObject &a) -> QJsonValue {
        const QString path = trackedPath(a), name = versionName(a);
        const QByteArray old = readVersion(path, name).value_or(QByteArray());
        if (isBinary(path, old))
            return QJsonObject{{"binary", true}, {"text", ""}};
        auto lines = [](const QByteArray &b) {
            QStringList l = QString::fromUtf8(b).split('\n');
            if (!l.isEmpty() && l.last().isEmpty())
                l.removeLast();
            return l;
        };
        const QStringList diff = unifiedDiff(lines(old), lines(readBytes(path).value_or(QByteArray())),
                                             QStringLiteral("versão ") + versionLabel(name),
                                             QStringLiteral("atual"));
        return QJsonObject{{"binary", false}, {"text", diff.join('\n')}};
    };
    ops["backup_manual"] = [](const QJsonObject &a) -> QJsonValue {
        const QString path = trackedPath(a);
        if (backupFile(path, QStringLiteral("Backup manual")).isEmpty())
            return false;
        logChange(path, QStringLiteral("Backup manual"), QStringLiteral("Cópia do estado atual do arquivo."));
        return true;
    };
    ops["backup_restore"] = [](const QJsonObject &a) -> QJsonValue {
        const QString path = trackedPath(a), name = versionName(a);
        const bool force = flag(a, "force", false);
        if (config::isViewOnly(path))
            invalid(path + QStringLiteral(" não pode ser restaurado isoladamente."));
        const auto data = readVersion(path, name);
        if (!data)
            invalid(QStringLiteral("Versão não encontrada."));
        if (path == config::smbConf() && !force) {
            const RunResult t = testConfBytes(*data);
            if (t.rc != 0)
                return QJsonObject{{"ok", false}, {"invalid", t.out}};
        }
        const bool wasActive = path == config::passdb() &&
                               serviceStates({QStringLiteral("smb")}).value(QStringLiteral("smb")).first ==
                                   QLatin1String("active");
        if (wasActive)
            run(QStringList{"systemctl", "stop"} + kServices);
        try {
            Tracker t(QStringLiteral("Restaurar versão ") + versionLabel(name), {path});
            writeFile(path, *data);
        } catch (...) {
            if (wasActive)
                run(QStringList{"systemctl", "start"} + kServices);
            throw;
        }
        if (wasActive)
            run(QStringList{"systemctl", "start"} + kServices);
        if (path == config::smbConf())
            run({"smbcontrol", "all", "reload-config"});
        return QJsonObject{{"ok", true}, {"invalid", ""}};
    };
    return ops;
}

}  // namespace

const OpMap &operations()
{
    static const OpMap ops = build();
    return ops;
}

}  // namespace sm
