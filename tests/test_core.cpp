// Testes do core (QtTest). Os caminhos do core são redirecionados para um
// diretório temporário em main(), antes de qualquer teste; uma trava aborta se
// config::smbConf() não apontar para lá, para nunca tocar no /etc/samba real.
//
// ``test_core --fake-helper <modo>`` faz o binário agir como um auxiliar falso
// (sem root), usado pelos testes do HelperClient.
#include "core/backups.h"
#include "core/config.h"
#include "core/ops.h"
#include "core/protocol.h"
#include "core/selinux.h"
#include "core/smbconf.h"
#include "core/sysutils.h"
#include "core/textdiff.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

using namespace sm;

namespace {

const QString kSample = QStringLiteral("# comentário do topo\n"
                                       "[global]\n"
                                       "\tworkgroup = CASA\n"
                                       "\t; desativado = sim\n"
                                       "\tsecurity = user\n"
                                       "\n"
                                       "[Dados]\n"
                                       "\tpath = /srv/dados\n"
                                       "\tread only = no\n");

Params params(std::initializer_list<QPair<QString, QString>> kv)
{
    Params p;
    for (const auto &[k, v] : kv)
        p.set(k, v);
    return p;
}

QStringList fakeHelper(const QString &mode)
{
    return {QCoreApplication::applicationFilePath(), QStringLiteral("--fake-helper"), mode};
}

int runFakeHelper(const QString &mode)
{
    if (mode == QLatin1String("serve")) {
        OpMap ops;
        ops["eco"] = [](const QJsonObject &a) -> QJsonValue { return a.value("x"); };
        ops["falha"] = [](const QJsonObject &) -> QJsonValue { throw std::runtime_error("divisão por zero"); };
        serveLoop(0, 1, ops);
        return 0;
    }
    if (mode == QLatin1String("die")) {
        std::fputs("{\"ready\":true}\n", stdout);
        std::fflush(stdout);
        char buf[256];
        if (::read(0, buf, sizeof buf) < 0)
            return 1;
        return 0;
    }
    if (mode == QLatin1String("exit126"))
        return 126;
    if (mode == QLatin1String("exit127")) {
        std::fputs("Error executing command as another user: No authentication agent found.\n", stderr);
        return 127;
    }
    return 2;
}

}  // namespace

class TestCore : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- smb.conf ----
    void roundtripPreservaComentarios() { QCOMPARE(SmbConf(kSample).dump(), kSample); }

    void getIgnoraMaiusculasEComentarios()
    {
        const SmbConf c(kSample);
        QVERIFY(c.get("dados") == params({{"path", "/srv/dados"}, {"read only", "no"}}));
        QVERIFY(!c.get("GLOBAL").contains("desativado"));
    }

    void setSubstituiECria()
    {
        SmbConf c(kSample);
        c.set("dados", params({{"path", "/srv/novo"}, {"comment", ""}}));
        c.set("fotos", params({{"path", "/srv/fotos"}}));
        QVERIFY(c.get("Dados") == params({{"path", "/srv/novo"}}));
        QCOMPARE(c.names(), QStringList({"global", "dados", "fotos"}));  // set() adota a grafia pedida
        QVERIFY(c.dump().contains("workgroup = CASA"));
    }

    void remove()
    {
        SmbConf c(kSample);
        c.remove("DADOS");
        QCOMPARE(c.names(), QStringList({"global"}));
    }

    void sharesPulaGlobal()
    {
        const auto s = SmbConf(kSample).shares();
        QCOMPARE(s.size(), 1);
        QCOMPARE(s[0].first, QStringLiteral("Dados"));
    }

    void setPreservaComentariosEFormatacao()
    {
        SmbConf c(kSample);
        Params p = c.get("global");
        p.set("workgroup", "LAB");
        p.set("netbios name", "SERVIDOR");
        c.set("global", p);
        QString expected = kSample;
        expected.replace("CASA", "LAB").replace("\tsecurity = user\n",
                                                "\tsecurity = user\n\tnetbios name = SERVIDOR\n");
        QCOMPARE(c.dump(), expected);
    }

    void setRemoveChavesAusentesERepetidas()
    {
        SmbConf c("[s]\n\tpath = /a\n\t# nota\n\tpath = /b\n\tbrowseable = no\n");
        QCOMPARE(c.get("s").value("path"), QStringLiteral("/b"));  // vale o último, como no Samba
        c.set("s", params({{"path", "/c"}}));
        QCOMPARE(c.dump(), QStringLiteral("[s]\n\tpath = /c\n\t# nota\n"));
    }

    void includeRepetidoPreservado()
    {
        const QString texto = "[global]\n\tinclude = /etc/samba/usershares.conf\n"
                              "\t# do cockpit:\n\tinclude = registry\n\tworkgroup = A\n";
        SmbConf c(texto);
        Params p = c.get("global");
        c.set("global", p);
        QCOMPARE(c.dump(), texto);
        p.set("workgroup", "B");
        c.set("global", p);
        QCOMPARE(c.dump(), QString(texto).replace("= A", "= B"));
    }

    void continuacaoDeLinha()
    {
        SmbConf c("[s]\n\tvalid users = ana, \\\n\t\tbeto\n\tpath = /a\n");
        QCOMPARE(c.get("s").value("valid users"), QStringLiteral("ana, beto"));
        c.set("s", params({{"valid users", "ana, beto"}, {"path", "/b"}}));
        QVERIFY(c.dump().contains("\t\tbeto\n"));  // inalterado: formatação mantida
        c.set("s", params({{"valid users", "carla"}, {"path", "/b"}}));
        QCOMPARE(c.dump(), QStringLiteral("[s]\n\tvalid users = carla\n\tpath = /b\n"));
    }

    void setRecusaInjecao()
    {
        SmbConf c(kSample);
        const QList<Params> casos = {params({{"comment", "x\n[global]\nguest ok = yes"}}),
                                     params({{"path\r", "/srv"}}),
                                     params({{"path", QString("/srv") + QChar(0)}})};
        for (const Params &p : casos)
            QVERIFY_THROWS_EXCEPTION(std::invalid_argument, c.set("novo", p));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, c.set("a]b", params({{"path", "/srv"}})));
        QCOMPARE(c.dump(), kSample);
    }

    void nomesReservados()
    {
        for (const char *n : {"global", "Global", "HOMES", "printers"})
            QVERIFY2(!shareNameError(n, {}, true).isEmpty(), n);
    }

    void duplicadoSoAoCriar()
    {
        QVERIFY(!shareNameError("dados", {"Dados"}, true).isEmpty());
        QVERIFY(shareNameError("dados", {"Dados"}, false).isEmpty());
        QVERIFY(!shareNameError("sumiu", {"Dados"}, false).isEmpty());
    }

    void caracteresDoNome()
    {
        QVERIFY(shareNameError("Minhas Fotos-2.0", {}, true).isEmpty());
        QVERIFY(!shareNameError("a/b", {}, true).isEmpty());
    }

    void controle()
    {
        QVERIFY(hasControlChars("a\nb"));
        QVERIFY(!hasControlChars("a\tb ção"));
    }

    // ---- diff ----
    void diffComoDifflib()
    {
        const QStringList d = unifiedDiff({"a", "b", "c"}, {"a", "x", "c", "d"}, "antes", "depois");
        QCOMPARE(d, QStringList({"--- antes", "+++ depois", "@@ -1,3 +1,4 @@", " a", "-b", "+x", " c", "+d"}));
        QVERIFY(unifiedDiff({"a"}, {"a"}, "x", "y").isEmpty());
    }

    // ---- pastas, SELinux, passdb ----
    void makeShareDirComDonoESetgid()
    {
        QTemporaryDir d;
        const QString alvo = d.filePath("samba/publico");
        const Owner owner{::getuid(), ::getgid()};
        QCOMPARE(makeShareDir(alvo, owner), QStringList({d.filePath("samba"), alvo}));
        struct stat st{};
        QCOMPARE(::stat(QFile::encodeName(alvo).constData(), &st), 0);
        QCOMPARE(st.st_uid, owner.uid);
        QCOMPARE(st.st_gid, owner.gid);
        QCOMPARE(st.st_mode & 07777, 02775u);
        QVERIFY(makeShareDir(alvo, owner).isEmpty());  // já existe: nada muda
    }

    void labelSistema()
    {
        for (const char *p : {"/", "/etc", "/etc/ssh", "/usr/share/x", "/var/lib/samba", "/root/a"})
            QVERIFY2(!labelBlockReason(p).isEmpty(), p);
    }

    void labelPermitidos()
    {
        for (const char *p : {"/srv/dados", "/mnt/hd/fotos", "/data"})
            QVERIFY2(labelBlockReason(p).isEmpty(), p);
    }

    void labelHomeInteiraESubpasta()
    {
        const QString home = QDir::homePath();
        if (home == "/" || home == "/root" || ::getuid() < 1000 || !QDir(home).exists())
            QSKIP("sem home de usuário comum");
        QVERIFY(labelBlockReason(home).contains("pasta pessoal"));
        QVERIFY(labelBlockReason(home + "/Compartilhado").isEmpty());
        QVERIFY(!labelBlockReason(home + "/.ssh").isEmpty());
    }

    void labelSymlinkEPastaComSsh()
    {
        QTemporaryDir d;
        QVERIFY(QFile::link("/etc", d.filePath("link")));
        QVERIFY(!labelBlockReason(d.filePath("link")).isEmpty());
        QVERIFY(QDir().mkpath(d.filePath("conta/.ssh")));
        QVERIFY(labelBlockReason(d.filePath("conta")).contains(".ssh"));
    }

    void passdbParseEDescricaoSemHashes()
    {
        const QString linhas = "joao:1001:XXXX:AAAA:[U          ]:LCT-1:\n"
                               "maria:1002:XXXX:BBBB:[DU         ]:LCT-2:\n";
        const PassdbEntries antes = parseSmbpasswd(linhas);
        QCOMPARE(antes.size(), 2);
        QVERIFY((antes["joao"] == PassdbEntry{"U", "AAAA", "LCT-1"}));
        QVERIFY((antes["maria"] == PassdbEntry{"DU", "BBBB", "LCT-2"}));
        PassdbEntries depois = antes;
        depois["joao"] = {"DU", "CCCC", "LCT-3"};
        depois["ana"] = {"U", "DDDD", "LCT-4"};
        depois.remove("maria");
        const QString texto = describePassdbChange(antes, depois);
        QCOMPARE(texto.split('\n'), QStringList({"Usuário adicionado: ana", "Usuário removido: maria",
                                                 "Usuário desabilitado: joao", "Senha alterada: joao"}));
        QVERIFY(!texto.contains("CCCC"));
    }

    // ---- protocolo ----
    void serveLoopProtocolo()
    {
        int in[2], out[2];
        QCOMPARE(::pipe(in), 0);
        QCOMPARE(::pipe(out), 0);
        const QByteArray reqs = "{\"op\":\"soma\",\"args\":{\"a\":2,\"b\":3}}\n"
                                "{\"op\":\"rm_rf\",\"args\":{}}\n"
                                "{\"op\":\"soma\",\"args\":{\"a\":1}}\n"
                                "lixo\n";
        QCOMPARE(::write(in[1], reqs.constData(), reqs.size()), reqs.size());
        ::close(in[1]);
        OpMap ops;
        ops["soma"] = [](const QJsonObject &a) -> QJsonValue {
            if (!a.value("b").isDouble())
                throw std::invalid_argument("falta b");
            return a.value("a").toInt() + a.value("b").toInt();
        };
        serveLoop(in[0], out[1], ops);
        ::close(in[0]);
        ::close(out[1]);
        QFile f;
        QVERIFY(f.open(out[0], QIODevice::ReadOnly, QFileDevice::AutoCloseHandle));
        const QList<QByteArray> lines = f.readAll().trimmed().split('\n');
        QCOMPARE(lines.size(), 5);
        auto obj = [&](int i) { return QJsonDocument::fromJson(lines[i]).object(); };
        QCOMPARE(obj(0), QJsonObject({{"ready", true}}));
        QCOMPARE(obj(1), QJsonObject({{"ok", true}, {"result", 5}}));
        QVERIFY(!obj(2)["ok"].toBool());
        QVERIFY(obj(2)["error"].toString().contains("desconhecida"));
        QVERIFY(!obj(3)["ok"].toBool());  // argumento faltando vira erro, auxiliar segue vivo
        QVERIFY(!obj(4)["ok"].toBool());
    }

    void helperChamadasEErros()
    {
        HelperClient c(fakeHelper("serve"));
        const QJsonArray x{QStringLiteral("ção"), 1};
        QCOMPARE(c.call("eco", {{"x", x}}), QJsonValue(x));
        QVERIFY_THROWS_EXCEPTION(BackendError, c.call("falha"));
        QCOMPARE(c.call("eco", {{"x", "ainda vivo"}}), QJsonValue("ainda vivo"));
    }

    void helperQueMorre()
    {
        HelperClient c(fakeHelper("die"));
        QVERIFY_THROWS_EXCEPTION(BackendError, c.call("qualquer"));
    }

    void helperQueNaoInicia()
    {
        try {
            HelperClient c(fakeHelper("exit126"));
            QFAIL("deveria falhar");
        } catch (const BackendError &e) {
            QVERIFY(e.message().contains("cancelada"));
        }
    }

    void falhaDoPkexecMostraOMotivo()
    {
        try {
            HelperClient c(fakeHelper("exit127"));
            QFAIL("deveria falhar");
        } catch (const BackendError &e) {
            QVERIFY(e.message().contains("Autorização negada"));
            QVERIFY(e.message().contains("No authentication agent found"));
        }
    }

    // ---- operações ----
    void listaFechada()
    {
        QVERIFY(!operations().contains("run"));
        QVERIFY(operations().contains("share_save"));
    }

    void argumentosInvalidosNaoExecutamNada()
    {
        LocalBackend b;
        const QString conf = config::smbConf();
        auto share = [](const QJsonArray &p) {
            return QJsonObject{{"name", "x"}, {"params", p}, {"selinux", false}, {"creating", true}};
        };
        const QList<QPair<QString, QJsonObject>> casos = {
            {"user_add", {{"user", "root; rm -rf /"}, {"password", "x"}}},
            {"user_add", {{"user", "joao"}, {"password", "a\nb"}}},
            {"user_remove", {{"user", "-x"}}},
            {"service_action", {{"action", "mask"}}},
            {"selinux_booleans", {{"kind", "tudo"}}},
            {"backup_view", {{"path", "/etc/shadow"}, {"name", "20260101-000000-000000.bak"}}},
            {"backup_view", {{"path", conf}, {"name", "../../etc/shadow"}}},
            {"backup_restore", {{"path", "/etc/passwd"}, {"name", "20260101-000000-000000.bak"}}},
            {"global_save", {{"values", QJsonObject{{"include", "/tmp/x"}}}}},
            {"share_save", share({QJsonArray{"path", "relativo"}})},
            {"share_save", share({QJsonArray{"path", "/srv"}, QJsonArray{"a\nb", "c"}})},
            {"share_save", {{"name", "x"}}},
        };
        for (const auto &[op, args] : casos)
            QVERIFY_THROWS_EXCEPTION(BackendError, b.call(op, args));
    }

    // ---- smb.conf de ponta a ponta (sandbox) ----
    void opsCriaEditaRemove()
    {
        if (!resetConf())
            QSKIP("testparm não instalado");
        LocalBackend b;
        QVERIFY(share(b, "Teste", true, "um"));
        QVERIFY(share(b, "Teste", false, "dois"));
        QCOMPARE(SmbConf::load().get("teste").value("comment"), QStringLiteral("dois"));
        QVERIFY(b.call("share_remove", {{"name", "teste"}}).toArray()[0].toBool());
        QCOMPARE(SmbConf::load().names(), QStringList({"global"}));
    }

    void opsNaoSobrescreveGlobalNemDuplica()
    {
        if (!resetConf())
            QSKIP("testparm não instalado");
        LocalBackend b;
        QVERIFY(!share(b, "Global", true));
        QVERIFY(share(b, "Dados", true));
        QVERIFY(!share(b, "dados", true));
        QCOMPARE(SmbConf::load().get("global").value("workgroup"), QStringLiteral("CASA"));
    }

    void opsGlobalSaveEBackup()
    {
        if (!resetConf())
            QSKIP("testparm não instalado");
        LocalBackend b;
        QVERIFY(b.call("global_save", {{"values", QJsonObject{{"workgroup", "LAB"}}}}).toArray()[0].toBool());
        QCOMPARE(SmbConf::load().get("global").value("workgroup"), QStringLiteral("LAB"));
        const QJsonArray versions = b.call("backup_list", {{"path", config::smbConf()}}).toArray();
        QVERIFY(!versions.isEmpty());
        const QString name = versions[0].toArray()[0].toString();
        const QJsonObject diff = b.call("backup_diff", {{"path", config::smbConf()}, {"name", name}}).toObject();
        QVERIFY(diff["text"].toString().contains("+\tworkgroup = LAB"));
        QVERIFY(b.call("backup_log", {{"path", config::smbConf()}}).toString().contains("Editar seção [global]"));

        // restaurar volta ao CASA e gera nova versão
        const QJsonObject r =
            b.call("backup_restore", {{"path", config::smbConf()}, {"name", name}}).toObject();
        QVERIFY(r["ok"].toBool());
        QCOMPARE(SmbConf::load().get("global").value("workgroup"), QStringLiteral("CASA"));
    }

private:
    static bool resetConf()
    {
        if (which("testparm").isEmpty())
            return false;
        QFile f(config::smbConf());
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write("[global]\n\tworkgroup = CASA\n");
        return true;
    }

    static bool share(Backend &b, const QString &name, bool creating, const QString &comment = {})
    {
        QJsonArray p{QJsonArray{"path", "/srv/teste"}};
        if (!comment.isEmpty())
            p.append(QJsonArray{"comment", comment});
        const QJsonValue r = b.call(
            "share_save", {{"name", name}, {"params", p}, {"selinux", false}, {"creating", creating}});
        return r.toArray()[0].toBool();
    }
};

int main(int argc, char **argv)
{
    if (argc == 3 && QByteArray(argv[1]) == "--fake-helper") {
        QCoreApplication app(argc, argv);
        return runFakeHelper(QString::fromLocal8Bit(argv[2]));
    }
    QTemporaryDir sandbox(QDir::tempPath() + "/sm-tests-XXXXXX");
    if (!sandbox.isValid())
        return 1;
    qputenv("SM_SMB_CONF", QFile::encodeName(sandbox.filePath("smb.conf")));
    qputenv("SM_PASSDB", QFile::encodeName(sandbox.filePath("passdb.tdb")));
    qputenv("SM_APP_DIR", QFile::encodeName(sandbox.filePath("app")));
    if (!config::smbConf().startsWith(sandbox.path())) {
        std::fputs("sandbox de testes não aplicado; abortando\n", stderr);
        return 1;
    }
    QCoreApplication app(argc, argv);
    TestCore t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_core.moc"
