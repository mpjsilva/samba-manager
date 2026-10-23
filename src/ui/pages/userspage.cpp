#include "ui/pages/userspage.h"

#include "ui/dialogs.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTimer>
#include <QTreeWidget>

namespace sm::ui {

QList<QPair<QString, bool>> parseUsers(const QString &out)
{
    QList<QPair<QString, bool>> users;
    QString user, flags;
    auto flush = [&] {
        if (!user.isEmpty())
            users.append({user, !flags.contains('D')});
    };
    for (const QString &line : out.split('\n')) {
        if (line.startsWith(QLatin1String("Unix username:"))) {
            flush();
            user = line.section(':', 1).trimmed();
            flags.clear();
        } else if (line.startsWith(QLatin1String("Account Flags:"))) {
            flags = line.section(':', 1).trimmed();
        }
    }
    flush();
    return users;
}

UsersPage::UsersPage(MainWindow &win)
    : Page(QStringLiteral("Usuários"), QStringLiteral("Contas autorizadas a acessar os compartilhamentos.")),
      m_win(win), m_debounce(new QTimer(this))
{
    QPushButton *add = button(QStringLiteral("+  Novo usuário"));
    connect(add, &QPushButton::clicked, this, &UsersPage::add);
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
    QPushButton *pw = smallButton(QStringLiteral("Alterar senha"), QStringLiteral("secondary"), QStringLiteral("solid"));
    QPushButton *en = smallButton(QStringLiteral("Habilitar"), QStringLiteral("secondary"), QStringLiteral("outline"));
    QPushButton *dis = smallButton(QStringLiteral("Desabilitar"), QStringLiteral("secondary"), QStringLiteral("outline"));
    QPushButton *rm = smallButton(QStringLiteral("Remover"), QStringLiteral("danger"), QStringLiteral("outline"));
    for (QPushButton *b : {pw, en, dis, rm})
        bar->addWidget(b);
    card->box()->addLayout(bar);
    card->box()->addSpacing(14);
    table = new TableBox({{"Usuário", 320, true}, {"Estado", 220, false}});
    card->box()->addWidget(table, 1);
    body()->addWidget(card, 1);

    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_debounce, &QTimer::timeout, this, [this] { fill(); });
    connect(filter, &QLineEdit::textChanged, m_debounce, qOverload<>(&QTimer::start));
    connect(pw, &QPushButton::clicked, this, &UsersPage::password);
    connect(en, &QPushButton::clicked, this, [this] { setEnabled(true); });
    connect(dis, &QPushButton::clicked, this, [this] { setEnabled(false); });
    connect(rm, &QPushButton::clicked, this, &UsersPage::remove);
    auto *del = new QShortcut(QKeySequence::Delete, table->tree(), nullptr, nullptr, Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, this, &UsersPage::remove);
}

void UsersPage::refresh()
{
    m_win.call(
        QStringLiteral("users_list"), {},
        [this](const QJsonValue &res) {
            const QJsonArray r = res.toArray();
            if (r.at(0).toInt(-1) != 0) {
                m_users.clear();
                fill(r.at(1).toString());
                return;
            }
            m_users = parseUsers(r.at(1).toString());
            fill();
        },
        false);
}

void UsersPage::fill(const QString &error)
{
    const QString q = filter->text().trimmed().toLower();
    QTreeWidget *tree = table->tree();
    const QString keep = table->selectedKey();
    tree->clear();
    const QColor on = theme::ink(theme::success), off = theme::muted;
    int shown = 0;
    for (const auto &[user, active] : std::as_const(m_users)) {
        if (!q.isEmpty() && !user.toLower().contains(q))
            continue;
        auto *item = new QTreeWidgetItem(
            QStringList{user, active ? QStringLiteral("● Ativo") : QStringLiteral("● Desabilitado")});
        item->setData(0, Qt::UserRole, user);
        item->setForeground(1, active ? on : off);
        if (!active)
            item->setForeground(0, off);
        tree->addTopLevelItem(item);
        if (user == keep)
            tree->setCurrentItem(item);
        ++shown;
    }
    if (!error.isEmpty())
        table->setEmpty(QStringLiteral("Não foi possível listar os usuários.\n") + error.left(160));
    else if (!shown)
        table->setEmpty(m_users.isEmpty()
                            ? QStringLiteral("Nenhum usuário Samba ainda.\nClique em “Novo usuário” para começar.")
                            : QStringLiteral("Nada encontrado para este filtro."));
    else
        table->setEmpty({});
}

QString UsersPage::selected()
{
    const QString u = table->selectedKey();
    if (u.isEmpty())
        m_win.toast(QStringLiteral("Selecione um usuário na lista."), QStringLiteral("warning"));
    return u;
}

void UsersPage::add()
{
    UserDialog ud(&m_win);
    if (ud.exec() != QDialog::Accepted)
        return;
    const QString user = ud.user();
    PasswordDialog pd(&m_win, QStringLiteral("Senha Samba de %1").arg(user));
    if (pd.exec() != QDialog::Accepted)
        return;
    m_win.call(QStringLiteral("user_add"), {{"user", user}, {"password", pd.password()}},
               [this, user](const QJsonValue &r) {
                   m_win.showResult(r, QStringLiteral("Usuário “%1” adicionado.").arg(user));
                   refresh();
               });
}

void UsersPage::password()
{
    const QString user = selected();
    if (user.isEmpty())
        return;
    PasswordDialog pd(&m_win, QStringLiteral("Nova senha de %1").arg(user));
    if (pd.exec() != QDialog::Accepted)
        return;
    m_win.call(QStringLiteral("user_password"), {{"user", user}, {"password", pd.password()}},
               [this](const QJsonValue &r) { m_win.showResult(r, QStringLiteral("Senha alterada.")); });
}

void UsersPage::setEnabled(bool enabled)
{
    const QString user = selected();
    if (user.isEmpty())
        return;
    m_win.call(QStringLiteral("user_set_enabled"), {{"user", user}, {"enabled", enabled}},
               [this, user, enabled](const QJsonValue &r) {
                   m_win.showResult(r, QStringLiteral("Usuário “%1” %2.")
                                           .arg(user, enabled ? QStringLiteral("habilitado")
                                                              : QStringLiteral("desabilitado")));
                   refresh();
               });
}

void UsersPage::remove()
{
    const QString user = selected();
    if (user.isEmpty() ||
        !confirm(&m_win, QStringLiteral("Remover usuário"),
                 QStringLiteral("Remover “%1” do Samba?\nO usuário Linux é mantido.").arg(user),
                 QStringLiteral("Remover"), true))
        return;
    m_win.call(QStringLiteral("user_remove"), {{"user", user}}, [this, user](const QJsonValue &r) {
        m_win.showResult(r, QStringLiteral("Usuário “%1” removido do Samba.").arg(user));
        refresh();
    });
}

}  // namespace sm::ui
