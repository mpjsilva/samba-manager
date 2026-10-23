// Página Serviço: estado smb/nmb, controle, firewall e SELinux.
#pragma once

#include "ui/widgets.h"

#include <QMap>

namespace sm::ui {

class MainWindow;

class ServicePage : public Page {
    Q_OBJECT
public:
    explicit ServicePage(MainWindow &win);
    void refresh() override;
    //! Pílulas (estado, inicialização) de cada serviço.
    QMap<QString, QPair<Pill *, Pill *>> pills;

private:
    void action(const QString &action);
    void openFirewall();
    void relabel();
    void selinuxHomes();
    void selinuxBroad();
    MainWindow &m_win;
};

}  // namespace sm::ui
