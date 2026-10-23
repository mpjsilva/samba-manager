// Página Compartilhamentos: lista com filtro, CRUD e testparm.
#pragma once

#include "core/smbconf.h"
#include "ui/widgets.h"

class QLineEdit;
class QTimer;

namespace sm::ui {

class MainWindow;

class SharesPage : public Page {
    Q_OBJECT
public:
    explicit SharesPage(MainWindow &win);
    void refresh() override { fill(true); }
    //! Redesenha a tabela; com ``reload`` relê o smb.conf (a interface só lê).
    void fill(bool reload);

    QLineEdit *filter;
    TableBox *table;

private:
    QString selected();
    void add();
    void edit();
    void remove();
    void apply(const QString &name, const Params &params, bool selinux, bool creating);
    MainWindow &m_win;
    SmbConf m_conf;
    QTimer *m_debounce;
};

}  // namespace sm::ui
