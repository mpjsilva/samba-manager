#include "ui/pages/backupspage.h"

#include "core/backups.h"
#include "core/config.h"
#include "ui/dialogs.h"
#include "ui/mainwindow.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QPushButton>
#include <QTreeWidget>

namespace sm::ui {

BackupsPage::BackupsPage(MainWindow &win)
    : Page(QStringLiteral("Backups e log"),
           QStringLiteral("Últimas %1 versões dos arquivos alterados e histórico de mudanças.")
               .arg(config::kMaxVersions)),
      m_win(win)
{
    auto *card = new Card;
    auto *bar = new QHBoxLayout;
    bar->setSpacing(8);
    bar->addWidget(label(QStringLiteral("Arquivo"), QStringLiteral("muted")));
    combo = new QComboBox;
    for (const auto &[p, d] : config::tracked())
        combo->addItem(QStringLiteral("%1  —  %2").arg(p, d), p);
    combo->setMinimumWidth(520);
    bar->addWidget(combo);
    bar->addStretch(1);
    card->box()->addLayout(bar);
    card->box()->addSpacing(12);

    table = new TableBox({{"Data/hora", 180, false}, {"Tamanho", 100, false}, {"Ação que gerou o backup", 500, true}});
    table->setFixedHeight(250);
    card->box()->addWidget(table);
    card->box()->addSpacing(12);

    auto *btns = new QHBoxLayout;
    btns->setSpacing(8);
    QPushButton *rest = smallButton(QStringLiteral("Restaurar"), QStringLiteral("primary"), QStringLiteral("solid"));
    QPushButton *viewB = smallButton(QStringLiteral("Ver conteúdo"), QStringLiteral("secondary"), QStringLiteral("solid"));
    QPushButton *cmp = smallButton(QStringLiteral("Comparar com atual"), QStringLiteral("secondary"), QStringLiteral("solid"));
    QPushButton *man = smallButton(QStringLiteral("Backup manual agora"), QStringLiteral("secondary"), QStringLiteral("outline"));
    btns->addWidget(rest);
    btns->addWidget(viewB);
    btns->addWidget(cmp);
    btns->addStretch(1);
    btns->addWidget(man);
    card->box()->addLayout(btns);
    card->box()->addSpacing(16);
    card->box()->addWidget(separator());
    card->box()->addSpacing(16);
    card->box()->addWidget(label(QStringLiteral("Log de alterações deste arquivo"), QStringLiteral("h2")));
    card->box()->addSpacing(8);
    log = new TextView;
    card->box()->addWidget(log, 1);
    body()->addWidget(card, 1);

    connect(combo, &QComboBox::currentIndexChanged, this, &BackupsPage::refresh);
    connect(rest, &QPushButton::clicked, this, &BackupsPage::restore);
    connect(viewB, &QPushButton::clicked, this, &BackupsPage::view);
    connect(cmp, &QPushButton::clicked, this, &BackupsPage::compare);
    connect(man, &QPushButton::clicked, this, &BackupsPage::manual);
    connect(table->tree(), &QTreeWidget::itemDoubleClicked, this, &BackupsPage::view);
}

QString BackupsPage::path() const { return combo->currentData().toString(); }

void BackupsPage::refresh()
{
    const QString p = path();
    Backend *b = &m_win.backend();
    m_win.bg(
        [b, p]() -> QJsonValue {
            return QJsonArray{b->call(QStringLiteral("backup_list"), {{"path", p}}),
                              b->call(QStringLiteral("backup_log"), {{"path", p}})};
        },
        [this, p](const QJsonValue &res) {
            if (p != path())
                return;  // o usuário trocou de arquivo enquanto carregava
            const QJsonArray versions = res.toArray().at(0).toArray();
            const QString text = res.toArray().at(1).toString();
            QTreeWidget *tree = table->tree();
            tree->clear();
            for (const QJsonValue &v : versions) {
                const QString name = v.toArray().at(0).toString();
                const QJsonObject meta = v.toArray().at(1).toObject();
                auto *item = new QTreeWidgetItem(QStringList{
                    versionLabel(name), QStringLiteral("%1 B").arg(meta.value("size").toInteger()),
                    meta.value("reason").toString()});
                item->setData(0, Qt::UserRole, name);
                tree->addTopLevelItem(item);
            }
            table->setEmpty(versions.isEmpty() ? QStringLiteral("Nenhuma versão guardada ainda.") : QString());
            log->setText(text.isEmpty() ? QStringLiteral("Nenhuma alteração registrada ainda.") : text, true);
        },
        false);
}

QString BackupsPage::selected()
{
    const QString n = table->selectedKey();
    if (n.isEmpty())
        m_win.toast(QStringLiteral("Selecione uma versão na lista."), QStringLiteral("warning"));
    return n;
}

void BackupsPage::view()
{
    const QString name = selected(), p = path();
    if (name.isEmpty())
        return;
    m_win.call(
        QStringLiteral("backup_view"), {{"path", p}, {"name", name}},
        [this, p, name](const QJsonValue &res) {
            const QJsonObject o = res.toObject();
            if (o.value("binary").toBool())
                return message(&m_win, QStringLiteral("Arquivo binário"),
                               QStringLiteral("Não é possível exibir o conteúdo de um arquivo binário."));
            showTextWindow(&m_win, QStringLiteral("%1 — %2").arg(p, versionLabel(name)), o.value("text").toString());
        },
        false);
}

void BackupsPage::compare()
{
    const QString name = selected(), p = path();
    if (name.isEmpty())
        return;
    m_win.call(
        QStringLiteral("backup_diff"), {{"path", p}, {"name", name}},
        [this](const QJsonValue &res) {
            const QJsonObject o = res.toObject();
            if (o.value("binary").toBool())
                return message(&m_win, QStringLiteral("Arquivo binário"),
                               QStringLiteral("Não há comparação textual para arquivos binários."));
            const QString text = o.value("text").toString();
            showTextWindow(&m_win, QStringLiteral("Diferenças (versão -> atual)"),
                           text.isEmpty() ? QStringLiteral("Sem diferenças.") : text);
        },
        false);
}

void BackupsPage::manual()
{
    const QString p = path();
    m_win.call(QStringLiteral("backup_manual"), {{"path", p}}, [this, p](const QJsonValue &ok) {
        if (!ok.toBool())
            return message(&m_win, QStringLiteral("Erro"), QStringLiteral("Não foi possível ler %1.").arg(p));
        m_win.toast(QStringLiteral("Backup criado."));
        refresh();
    });
}

void BackupsPage::restore()
{
    const QString name = selected(), p = path();
    if (name.isEmpty())
        return;
    if (config::isViewOnly(p))
        return message(&m_win, QStringLiteral("Restauração indisponível"),
                       QStringLiteral("%1 não pode ser restaurado isoladamente: /etc/shadow e /etc/gshadow "
                                      "também mudam junto e ficariam inconsistentes.\n\nUse “Ver conteúdo” ou "
                                      "“Comparar com atual” e corrija manualmente (vipw / vigr).")
                           .arg(p));
    if (!confirm(&m_win, QStringLiteral("Restaurar versão"),
                 QStringLiteral("Restaurar %1\npara a versão de %2?\nO estado atual também será guardado como backup.")
                     .arg(p, versionLabel(name)),
                 QStringLiteral("Restaurar")))
        return;
    doRestore(p, name, false);
}

void BackupsPage::doRestore(const QString &p, const QString &name, bool force)
{
    m_win.call(QStringLiteral("backup_restore"), {{"path", p}, {"name", name}, {"force", force}},
               [this, p, name](const QJsonValue &res) {
                   const QJsonObject o = res.toObject();
                   if (!o.value("ok").toBool()) {
                       // smb.conf com erros de sintaxe: só segue com confirmação explícita.
                       if (confirm(&m_win, QStringLiteral("Configuração com erros"),
                                   QStringLiteral("Esta versão tem erros de sintaxe:\n%1\n\nRestaurar mesmo assim?")
                                       .arg(o.value("invalid").toString()),
                                   QStringLiteral("Restaurar mesmo assim"), true))
                           doRestore(p, name, true);
                       return;
                   }
                   m_win.toast(QStringLiteral("Versão restaurada."));
                   refresh();
                   m_win.refresh(MainWindow::Users);
               });
}

}  // namespace sm::ui
