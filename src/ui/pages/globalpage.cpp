#include "ui/pages/globalpage.h"

#include "core/smbconf.h"
#include "ui/dialogs.h"
#include "ui/mainwindow.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <array>

namespace sm::ui {

GlobalPage::GlobalPage(MainWindow &win)
    : Page(QStringLiteral("Configurações globais"), QStringLiteral("Identificação do servidor na rede.")), m_win(win)
{
    auto *card = new Card;
    const QList<std::array<QString, 3>> defs = {
        {"workgroup", "Workgroup", "Nome do grupo de trabalho (padrão: WORKGROUP)."},
        {"server string", "Descrição do servidor", "Texto exibido ao navegar na rede."},
        {"netbios name", "Nome NetBIOS", "Nome do servidor na rede. Vazio = nome do host."},
    };
    for (int i = 0; i < defs.size(); ++i) {
        if (i)
            card->box()->addSpacing(16);
        card->box()->addWidget(label(defs[i][1], QStringLiteral("bold")));
        card->box()->addSpacing(5);
        auto *e = new QLineEdit;
        card->box()->addWidget(e);
        card->box()->addSpacing(4);
        card->box()->addWidget(label(defs[i][2], QStringLiteral("small")));
        fields[defs[i][0]] = e;
    }
    card->box()->addSpacing(22);
    QPushButton *save = button(QStringLiteral("Salvar alterações"));
    connect(save, &QPushButton::clicked, this, &GlobalPage::save);
    card->box()->addWidget(save, 0, Qt::AlignRight);
    body()->addWidget(card);
    body()->addStretch(1);
}

void GlobalPage::refresh()
{
    const Params p = SmbConf::load().get(QStringLiteral("global"));
    for (auto it = fields.cbegin(); it != fields.cend(); ++it)
        it.value()->setText(p.value(it.key()));
}

void GlobalPage::save()
{
    QJsonObject values;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        const QString v = it.value()->text().trimmed();
        if (hasControlChars(v)) {
            message(&m_win, QStringLiteral("Valor inválido"), QStringLiteral("Os campos não podem conter quebras de linha."));
            return;
        }
        values[it.key()] = v;
    }
    m_win.call(QStringLiteral("global_save"), {{"values", values}}, [this](const QJsonValue &res) {
        const QJsonArray r = res.toArray();
        if (r.at(0).toBool())
            m_win.toast(QStringLiteral("Configurações globais salvas."));
        else
            message(&m_win, QStringLiteral("Não foi possível salvar"), r.at(1).toString());
        refresh();
    });
}

}  // namespace sm::ui
