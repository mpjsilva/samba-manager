#include "ui/pages/servicepage.h"

#include "ui/dialogs.h"
#include "ui/mainwindow.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QPushButton>
#include <QVBoxLayout>

#include <array>

namespace sm::ui {

ServicePage::ServicePage(MainWindow &win)
    : Page(QStringLiteral("Serviço"), QStringLiteral("Estado do Samba, inicialização e segurança do sistema.")),
      m_win(win)
{
    QPushButton *upd = button(QStringLiteral("Atualizar"), QStringLiteral("secondary"), QStringLiteral("outline"));
    connect(upd, &QPushButton::clicked, this, &ServicePage::refresh);
    actions()->addWidget(upd);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    const QList<std::array<QString, 3>> info = {
        {"smb", "smb.service", "Servidor de arquivos e impressoras (SMB/CIFS)."},
        {"nmb", "nmb.service", "Resolução de nomes NetBIOS na rede local."},
    };
    for (int col = 0; col < info.size(); ++col) {
        auto *card = new Card;
        card->box()->addWidget(label(info[col][1], QStringLiteral("h2")));
        card->box()->addSpacing(2);
        card->box()->addWidget(label(info[col][2], QStringLiteral("muted")));
        card->box()->addSpacing(14);
        auto *row = new QHBoxLayout;
        row->setSpacing(8);
        auto *st = new Pill, *boot = new Pill;
        row->addWidget(st);
        row->addWidget(boot);
        row->addStretch(1);
        card->box()->addLayout(row);
        pills[info[col][0]] = {st, boot};
        grid->addWidget(card, 0, col);
    }
    body()->addLayout(grid);

    auto *ctl = new Card;
    ctl->box()->addWidget(label(QStringLiteral("Controle do serviço"), QStringLiteral("h2")));
    ctl->box()->addSpacing(2);
    ctl->box()->addWidget(label(QStringLiteral("As ações valem para smb e nmb ao mesmo tempo."), QStringLiteral("muted")));
    ctl->box()->addSpacing(14);
    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    const QList<std::array<QString, 4>> buttons = {
        {"Iniciar", "start", "success", "solid"},
        {"Reiniciar", "restart", "primary", "solid"},
        {"Parar", "stop", "danger", "outline"},
        {"", "", "", ""},
        {"Iniciar com o sistema", "enable", "secondary", "outline"},
        {"Não iniciar com o sistema", "disable", "secondary", "outline"},
    };
    for (const auto &[text, act, kind, variant] : buttons) {
        if (text.isEmpty()) {
            row->addSpacing(16);
            continue;
        }
        QPushButton *b = button(text, kind, variant);
        connect(b, &QPushButton::clicked, this, [this, act] { action(act); });
        row->addWidget(b);
    }
    row->addStretch(1);
    ctl->box()->addLayout(row);
    body()->addWidget(ctl);

    auto *net = new Card;
    net->box()->addWidget(label(QStringLiteral("Rede e segurança"), QStringLiteral("h2")));
    struct Item {
        QString title, desc, button;
        void (ServicePage::*slot)();
    };
    const QList<Item> items = {
        {"Firewall (firewalld)", "Libera o serviço 'samba' de forma permanente.", "Liberar no firewall",
         &ServicePage::openFirewall},
        {"SELinux: rótulos das pastas",
         "Aplica samba_share_t em todas as pastas compartilhadas (recomendado).", "Reaplicar rótulos",
         &ServicePage::relabel},
        {"SELinux: pastas pessoais",
         "Permite compartilhar diretórios /home sem rotulá-los (samba_enable_home_dirs).", "Permitir homes",
         &ServicePage::selinuxHomes},
        {"SELinux: permissões amplas (avançado)",
         "Deixa o smbd acessar qualquer arquivo. Menos seguro; use só se os rótulos não bastarem.",
         "Ativar booleans", &ServicePage::selinuxBroad},
    };
    for (const Item &it : items) {
        net->box()->addSpacing(14);
        net->box()->addWidget(separator());
        net->box()->addSpacing(14);
        auto *r = new QHBoxLayout;
        auto *txt = new QVBoxLayout;
        txt->setSpacing(2);
        txt->addWidget(label(it.title, QStringLiteral("bold")));
        QLabel *d = label(it.desc, QStringLiteral("muted"));
        d->setWordWrap(true);
        txt->addWidget(d);
        r->addLayout(txt, 1);
        QPushButton *b = button(it.button, QStringLiteral("secondary"), QStringLiteral("outline"));
        connect(b, &QPushButton::clicked, this, it.slot);
        r->addWidget(b, 0, Qt::AlignVCenter);
        net->box()->addLayout(r);
    }
    body()->addWidget(net);
    body()->addStretch(1);
}

void ServicePage::refresh()
{
    m_win.call(
        QStringLiteral("service_status"), {},
        [this](const QJsonValue &res) {
            static const QMap<QString, QPair<QString, QString>> states = {
                {"active", {"● Ativo", "success"}},
                {"inactive", {"● Parado", "danger"}},
                {"failed", {"● Falhou", "danger"}},
                {"activating", {"● Iniciando", "warning"}},
            };
            const QJsonObject o = res.toObject();
            for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
                if (!pills.contains(it.key()))
                    continue;
                const QJsonArray a = it.value().toArray();
                const QString act = a.at(0).toString(), en = a.at(1).toString();
                const auto st = states.value(act, {QStringLiteral("● ") + act, QStringLiteral("muted")});
                pills[it.key()].first->set(st.first, st.second);
                if (en == QLatin1String("enabled"))
                    pills[it.key()].second->set(QStringLiteral("Inicia com o sistema"), QStringLiteral("info"));
                else
                    pills[it.key()].second->set(QStringLiteral("Não inicia com o sistema"), QStringLiteral("muted"));
            }
        },
        false);
}

void ServicePage::action(const QString &act)
{
    static const QMap<QString, QString> msgs = {
        {"start", "Serviço iniciado."},
        {"stop", "Serviço parado."},
        {"restart", "Serviço reiniciado."},
        {"enable", "Samba iniciará junto com o sistema."},
        {"disable", "Samba não iniciará mais com o sistema."},
    };
    m_win.call(QStringLiteral("service_action"), {{"action", act}}, [this, act](const QJsonValue &r) {
        m_win.showResult(r, msgs.value(act));
        refresh();
    });
}

void ServicePage::openFirewall()
{
    m_win.call(QStringLiteral("firewall_open"), {},
               [this](const QJsonValue &r) { m_win.showResult(r, QStringLiteral("Samba liberado no firewall.")); });
}

void ServicePage::relabel()
{
    m_win.call(QStringLiteral("selinux_relabel"), {}, [this](const QJsonValue &res) {
        const QJsonArray results = res.toArray();
        if (results.isEmpty()) {
            m_win.toast(QStringLiteral("Nenhum compartilhamento com pasta definida."), QStringLiteral("warning"));
            return;
        }
        QStringList fails;
        for (const QJsonValue &v : results)
            if (!v.toArray().at(0).toBool())
                fails << v.toArray().at(1).toString();
        if (!fails.isEmpty())
            message(&m_win, QStringLiteral("Alguns rótulos não foram aplicados"), fails.join('\n'));
        else
            m_win.toast(QStringLiteral("Rótulos SELinux aplicados em %1 pasta(s).").arg(results.size()));
        m_win.refresh(MainWindow::Backups);
    });
}

void ServicePage::selinuxHomes()
{
    m_win.call(QStringLiteral("selinux_booleans"), {{"kind", "homes"}}, [this](const QJsonValue &r) {
        m_win.showResult(r, QStringLiteral("Compartilhamento de pastas pessoais permitido."));
    });
}

void ServicePage::selinuxBroad()
{
    if (!confirm(&m_win, QStringLiteral("Ativar permissões amplas?"),
                 QStringLiteral("Isso habilita samba_enable_home_dirs e samba_export_all_rw: o smbd poderá "
                                "ler e escrever em praticamente qualquer arquivo do sistema.\n\nO recomendado "
                                "é rotular as pastas compartilhadas (botão “Reaplicar rótulos”)."),
                 QStringLiteral("Ativar mesmo assim"), true))
        return;
    m_win.call(QStringLiteral("selinux_booleans"), {{"kind", "broad"}}, [this](const QJsonValue &r) {
        m_win.showResult(r, QStringLiteral("Permissões SELinux ativadas."));
    });
}

}  // namespace sm::ui
