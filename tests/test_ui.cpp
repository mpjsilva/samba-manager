// Testes da interface (QtTest, QT_QPA_PLATFORM=offscreen) com um backend
// falso: nada roda como root e nenhum comando do sistema é chamado.
#include "core/config.h"
#include "ui/dialogs.h"
#include "ui/mainwindow.h"
#include "ui/pages/backupspage.h"
#include "ui/pages/globalpage.h"
#include "ui/pages/servicepage.h"
#include "ui/pages/sharespage.h"
#include "ui/pages/statuspage.h"
#include "ui/pages/userspage.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

#include <QThread>

#include <mutex>

using namespace sm;
using namespace sm::ui;

namespace {

class FakeBackend : public Backend {
public:
    QJsonValue call(const QString &op, const QJsonObject &args) override
    {
        std::lock_guard g(m);
        calls << qMakePair(op, args);
        if (op == QLatin1String("service_status"))
            return QJsonObject{{"smb", QJsonArray{"active", "enabled"}}, {"nmb", QJsonArray{"inactive", "disabled"}}};
        if (op == QLatin1String("users_list"))
            return QJsonArray{0, "Unix username:        ana\nAccount Flags:        [U          ]\n"
                                 "Unix username:        beto\nAccount Flags:        [DU         ]\n"};
        if (op == QLatin1String("smbstatus"))
            return QJsonArray{0, ""};
        if (op == QLatin1String("backup_list"))
            return QJsonArray{QJsonArray{"20260102-030405-000006.bak",
                                         QJsonObject{{"reason", "Editar seção [global]"}, {"size", 42}}}};
        if (op == QLatin1String("backup_log"))
            return QStringLiteral("[2026-01-02 03:04:05] Editar seção [global]\n");
        if (op == QLatin1String("lento")) {
            QThread::msleep(300);
            return true;
        }
        return QJsonArray{0, ""};
    }
    QStringList ops()
    {
        std::lock_guard g(m);
        QStringList out;
        for (const auto &c : calls)
            out << c.first;
        return out;
    }
    std::mutex m;
    QList<QPair<QString, QJsonObject>> calls;
};

}  // namespace

class TestUi : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QFile f(config::smbConf());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[global]\n\tworkgroup = CASA\n\n[Dados]\n\tpath = /srv/dados\n\tread only = yes\n"
                "\tvalid users = ana\n\n[Fotos]\n\tpath = /srv/fotos\n\tguest ok = yes\n");
    }

    void parseUsersBlocos()
    {
        const auto u = parseUsers("Unix username: ana\nAccount Flags: [U ]\nUnix username: beto\n"
                                  "Account Flags: [DU ]\nUnix username: caio\n");
        QCOMPARE(u.size(), 3);
        QCOMPARE(u[0], qMakePair(QStringLiteral("ana"), true));
        QCOMPARE(u[1], qMakePair(QStringLiteral("beto"), false));
        QCOMPARE(u[2], qMakePair(QStringLiteral("caio"), true));
    }

    void janelaAbreNoServico()
    {
        FakeBackend b;
        MainWindow w(b);
        QCOMPARE(w.currentPage(), MainWindow::Service);
        QTRY_COMPARE(w.pending(), 0);
        auto *svc = static_cast<ServicePage *>(w.page(MainWindow::Service));
        QCOMPARE(svc->pills["smb"].first->text(), QStringLiteral("● Ativo"));
        QCOMPARE(svc->pills["nmb"].second->text(), QStringLiteral("Não inicia com o sistema"));
    }

    void compartilhamentosLidosDoSmbConf()
    {
        FakeBackend b;
        MainWindow w(b);
        w.showPage(MainWindow::Shares);
        auto *pg = static_cast<SharesPage *>(w.page(MainWindow::Shares));
        QTreeWidget *t = pg->table->tree();
        QCOMPARE(t->topLevelItemCount(), 2);
        QCOMPARE(t->topLevelItem(0)->text(2), QStringLiteral("Leitura"));
        QCOMPARE(t->topLevelItem(0)->text(4), QStringLiteral("ana"));
        QCOMPARE(t->topLevelItem(1)->text(3), QStringLiteral("Sim"));
        pg->filter->setText("foto");
        QTRY_COMPARE(t->topLevelItemCount(), 1);
        pg->filter->setText("nada");
        QTRY_VERIFY(!pg->table->emptyText().isEmpty());
        QVERIFY(pg->table->emptyText().contains("filtro"));
    }

    void usuariosComEstado()
    {
        FakeBackend b;
        MainWindow w(b);
        w.showPage(MainWindow::Users);
        QTRY_COMPARE(w.pending(), 0);
        auto *pg = static_cast<UsersPage *>(w.page(MainWindow::Users));
        QTreeWidget *t = pg->table->tree();
        QCOMPARE(t->topLevelItemCount(), 2);
        QCOMPARE(t->topLevelItem(1)->text(1), QStringLiteral("● Desabilitado"));
    }

    void globalCarregaCampos()
    {
        FakeBackend b;
        MainWindow w(b);
        w.showPage(MainWindow::Global);
        auto *pg = static_cast<GlobalPage *>(w.page(MainWindow::Global));
        QCOMPARE(pg->fields["workgroup"]->text(), QStringLiteral("CASA"));
        QVERIFY(pg->fields["netbios name"]->text().isEmpty());
    }

    void conexoesSemSessao()
    {
        FakeBackend b;
        MainWindow w(b);
        w.showPage(MainWindow::Status);
        QTRY_COMPARE(w.pending(), 0);
        auto *pg = static_cast<StatusPage *>(w.page(MainWindow::Status));
        QCOMPARE(pg->text->toPlainText(), QStringLiteral("Nenhuma conexão ativa."));
    }

    void backupsListaELog()
    {
        FakeBackend b;
        MainWindow w(b);
        w.showPage(MainWindow::Backups);
        QTRY_COMPARE(w.pending(), 0);
        auto *pg = static_cast<BackupsPage *>(w.page(MainWindow::Backups));
        QCOMPARE(pg->combo->count(), config::tracked().size());
        QCOMPARE(pg->path(), config::smbConf());
        QCOMPARE(pg->table->tree()->topLevelItemCount(), 1);
        QCOMPARE(pg->table->tree()->topLevelItem(0)->text(0), QStringLiteral("02/01/2026 03:04:05"));
        QVERIFY(pg->log->toPlainText().contains("Editar seção"));
    }

    void ocupadoRecusaSegundaOperacao()
    {
        FakeBackend b;
        MainWindow w(b);
        QTRY_COMPARE(w.pending(), 0);
        int done = 0;
        w.call("lento", {}, [&](const QJsonValue &) { ++done; });
        QVERIFY(w.isBusy());
        w.call("lento", {}, [&](const QJsonValue &) { ++done; });  // recusada (toast)
        QTRY_COMPARE(w.pending(), 0);
        QCOMPARE(done, 1);
        QCOMPARE(b.ops().count("lento"), 1);
        QVERIFY(!w.isBusy());
    }

    void shareDialogValida()
    {
        ShareDialog novo(nullptr, {"global", "Dados"});
        novo.nameEdit->setText("global");
        novo.pathEdit->setText("/srv/x");
        QCOMPARE(novo.problem().first, QStringLiteral("Nome inválido"));
        novo.nameEdit->setText("dados");
        QCOMPARE(novo.problem().first, QStringLiteral("Nome inválido"));
        novo.nameEdit->setText("Musica");
        novo.pathEdit->setText("relativo");
        QCOMPARE(novo.problem().first, QStringLiteral("Caminho inválido"));
        novo.pathEdit->setText("/etc");
        QCOMPARE(novo.problem().first, QStringLiteral("Pasta não pode ser rotulada"));
        novo.selinuxBox->setChecked(false);
        QVERIFY(novo.problem().first.isEmpty());
        novo.commentEdit->setText("a\nb");
        QCOMPARE(novo.problem().first, QStringLiteral("Valor inválido"));
    }

    void shareDialogPreservaExtras()
    {
        Params p;
        p.set("path", "/srv/dados");
        p.set("create mask", "0664");
        p.set("read only", "yes");
        ShareDialog d(nullptr, {"Dados"}, "Dados", p);
        QVERIFY(!d.nameEdit->isEnabled());
        QVERIFY(d.readOnly->isChecked());
        QVERIFY(d.browseable->isChecked());
        const Params out = d.params();
        QCOMPARE(out.value("create mask"), QStringLiteral("0664"));
        QCOMPARE(out.value("read only"), QStringLiteral("yes"));
        QCOMPARE(out.items().first().first, QStringLiteral("comment"));
        QVERIFY(d.problem().first.isEmpty());
    }

    //! Com SM_SCREENSHOT_DIR definido, salva uma captura de cada página.
    void capturas()
    {
        const QString dir = qEnvironmentVariable("SM_SCREENSHOT_DIR");
        if (dir.isEmpty())
            QSKIP("SM_SCREENSHOT_DIR não definido");
        FakeBackend b;
        MainWindow w(b);
        w.show();
        for (int i = 0; i < MainWindow::PageCount; ++i) {
            w.showPage(MainWindow::PageId(i));
            QTRY_COMPARE(w.pending(), 0);
            QTest::qWait(50);
            QVERIFY(w.grab().save(QStringLiteral("%1/page%2.png").arg(dir).arg(i)));
        }
        ShareDialog d(&w, {"Dados"});
        d.show();
        QTest::qWait(50);
        QVERIFY(d.grab().save(dir + "/share-dialog.png"));
    }

    void usuarioValido()
    {
        QVERIFY(UserDialog::isValidUser("joao_2"));
        QVERIFY(!UserDialog::isValidUser("Joao"));
        QVERIFY(!UserDialog::isValidUser("-x"));
        QVERIFY(!UserDialog::isValidUser(QString(33, 'a')));
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir sandbox;
    if (!sandbox.isValid())
        return 1;
    qputenv("SM_SMB_CONF", QFile::encodeName(sandbox.filePath("smb.conf")));
    qputenv("SM_PASSDB", QFile::encodeName(sandbox.filePath("passdb.tdb")));
    qputenv("SM_APP_DIR", QFile::encodeName(sandbox.filePath("app")));
    if (!config::smbConf().startsWith(sandbox.path()))
        return 1;
    QApplication app(argc, argv);
    theme::apply(app);
    TestUi t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_ui.moc"
