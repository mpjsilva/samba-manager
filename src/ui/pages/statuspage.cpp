#include "ui/pages/statuspage.h"

#include "ui/mainwindow.h"

#include <QJsonArray>
#include <QPushButton>
#include <QVBoxLayout>

namespace sm::ui {

StatusPage::StatusPage(MainWindow &win)
    : Page(QStringLiteral("Conexões ativas"),
           QStringLiteral("Sessões, compartilhamentos em uso e arquivos abertos (smbstatus).")),
      text(new TextView), m_win(win)
{
    QPushButton *upd = button(QStringLiteral("Atualizar"), QStringLiteral("secondary"), QStringLiteral("outline"));
    connect(upd, &QPushButton::clicked, this, &StatusPage::refresh);
    actions()->addWidget(upd);
    auto *card = new Card;
    card->box()->addWidget(text, 1);
    body()->addWidget(card, 1);
}

void StatusPage::refresh()
{
    m_win.call(
        QStringLiteral("smbstatus"), {},
        [this](const QJsonValue &r) {
            const QString out = r.toArray().at(1).toString();
            text->setText(out.isEmpty() ? QStringLiteral("Nenhuma conexão ativa.") : out);
        },
        false);
}

}  // namespace sm::ui
