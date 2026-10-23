#include "ui/pages/sharespage.h"

#include "ui/dialogs.h"
#include "ui/mainwindow.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTimer>
#include <QTreeWidget>

namespace sm::ui {

SharesPage::SharesPage(MainWindow &win)
    : Page(QStringLiteral("Compartilhamentos"), QStringLiteral("Pastas publicadas na rede.")), m_win(win),
      m_debounce(new QTimer(this))
{
    QPushButton *add = button(QStringLiteral("+  Novo compartilhamento"));
    connect(add, &QPushButton::clicked, this, &SharesPage::add);
    actions()->addWidget(add);

    auto *card = new Card;
    auto *bar = new QHBoxLayout;
    bar->setSpacing(8);
    bar->addWidget(label(QStringLiteral("Filtrar"), QStringLiteral("muted")));
    filter = new QLineEdit;
    filter->setFixedWidth(260);
    filter->setClearButtonEnabled(true);
    bar->addWidget(filter);
    bar->addStretch(1);
    QPushButton *test = smallButton(QStringLiteral("Validar config"), QStringLiteral("secondary"), QStringLiteral("ghost"));
    QPushButton *edit = smallButton(QStringLiteral("Editar"), QStringLiteral("secondary"), QStringLiteral("solid"));
    QPushButton *rm = smallButton(QStringLiteral("Remover"), QStringLiteral("danger"), QStringLiteral("outline"));
    bar->addWidget(test);
    bar->addWidget(edit);
    bar->addWidget(rm);
    card->box()->addLayout(bar);
    card->box()->addSpacing(14);
    table = new TableBox({{"Nome", 150, false},
                          {"Pasta", 300, true},
                          {"Acesso", 140, false},
                          {"Convidados", 100, false},
                          {"Usuários válidos", 200, true}});
    card->box()->addWidget(table, 1);
    body()->addWidget(card, 1);

    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_debounce, &QTimer::timeout, this, [this] { fill(false); });
    connect(filter, &QLineEdit::textChanged, m_debounce, qOverload<>(&QTimer::start));
    connect(test, &QPushButton::clicked, this, [this] {
        m_win.call(QStringLiteral("testparm"), {}, [this](const QJsonValue &r) {
            const QString out = r.toArray().at(1).toString();
            message(&m_win, QStringLiteral("Resultado do testparm"), out.isEmpty() ? QStringLiteral("Sem saída.") : out);
        });
    });
    connect(edit, &QPushButton::clicked, this, &SharesPage::edit);
    connect(rm, &QPushButton::clicked, this, &SharesPage::remove);
    connect(table->tree(), &QTreeWidget::itemDoubleClicked, this, &SharesPage::edit);
    auto *del = new QShortcut(QKeySequence::Delete, table->tree(), nullptr, nullptr, Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, this, &SharesPage::remove);
}

void SharesPage::fill(bool reload)
{
    if (reload)
        m_conf = SmbConf::load();
    const QString q = filter->text().trimmed().toLower();
    QTreeWidget *tree = table->tree();
    const QString keep = table->selectedKey();
    tree->clear();
    int total = 0;
    QList<QTreeWidgetItem *> rows;
    for (const auto &[n, p] : m_conf.shares()) {
        ++total;
        const QString path = p.value(QStringLiteral("path"));
        if (!q.isEmpty() && !n.toLower().contains(q) && !path.toLower().contains(q))
            continue;
        const QString users = p.value(QStringLiteral("valid users"));
        auto *item = new QTreeWidgetItem(QStringList{
            n, path,
            isYes(p.value(QStringLiteral("read only"), "no")) ? QStringLiteral("Leitura") : QStringLiteral("Leitura e escrita"),
            isYes(p.value(QStringLiteral("guest ok"), "no")) ? QStringLiteral("Sim") : QStringLiteral("Não"),
            users.isEmpty() ? QStringLiteral("Todos") : users});
        item->setData(0, Qt::UserRole, n);
        rows << item;
    }
    tree->addTopLevelItems(rows);
    for (QTreeWidgetItem *it : rows)
        if (it->data(0, Qt::UserRole).toString() == keep)
            tree->setCurrentItem(it);
    if (!rows.isEmpty())
        table->setEmpty({});
    else if (!q.isEmpty() && total)
        table->setEmpty(QStringLiteral("Nada encontrado para este filtro."));
    else
        table->setEmpty(QStringLiteral("Nenhum compartilhamento ainda.\nClique em “Novo compartilhamento” para começar."));
}

QString SharesPage::selected()
{
    const QString n = table->selectedKey();
    if (n.isEmpty())
        m_win.toast(QStringLiteral("Selecione um compartilhamento na lista."), QStringLiteral("warning"));
    return n;
}

void SharesPage::apply(const QString &name, const Params &params, bool selinux, bool creating)
{
    QJsonArray p;
    for (const auto &[k, v] : params.items())
        p.append(QJsonArray{k, v});
    m_win.call(QStringLiteral("share_save"),
               {{"name", name}, {"params", p}, {"selinux", selinux}, {"creating", creating}},
               [this, name](const QJsonValue &res) {
                   const QJsonArray r = res.toArray();
                   if (r.at(0).toBool())
                       m_win.toast(QStringLiteral("Compartilhamento “%1” salvo.").arg(name));
                   else
                       message(&m_win, QStringLiteral("Atenção"), r.at(1).toString());
                   fill(true);
               });
}

void SharesPage::add()
{
    m_conf = SmbConf::load();
    ShareDialog dlg(&m_win, m_conf.names());
    if (dlg.exec() == QDialog::Accepted)
        apply(dlg.name(), dlg.params(), dlg.selinux(), true);
}

void SharesPage::edit()
{
    const QString n = selected();
    if (n.isEmpty())
        return;
    m_conf = SmbConf::load();
    ShareDialog dlg(&m_win, m_conf.names(), n, m_conf.get(n));
    if (dlg.exec() == QDialog::Accepted)
        apply(dlg.name(), dlg.params(), dlg.selinux(), false);
}

void SharesPage::remove()
{
    const QString n = selected();
    if (n.isEmpty() ||
        !confirm(&m_win, QStringLiteral("Remover compartilhamento"),
                 QStringLiteral("Remover “%1” do Samba?\nOs arquivos da pasta não serão apagados.").arg(n),
                 QStringLiteral("Remover"), true))
        return;
    m_win.call(QStringLiteral("share_remove"), {{"name", n}}, [this, n](const QJsonValue &res) {
        const QJsonArray r = res.toArray();
        if (r.at(0).toBool())
            m_win.toast(QStringLiteral("Compartilhamento “%1” removido.").arg(n));
        else
            message(&m_win, QStringLiteral("Erro"), r.at(1).toString());
        fill(true);
    });
}

}  // namespace sm::ui
