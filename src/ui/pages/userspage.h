// Página Usuários: lista via pdbedit, filtro e CRUD de contas Samba.
#pragma once

#include "ui/widgets.h"

class QLineEdit;
class QTimer;

namespace sm::ui {

class MainWindow;

//! Saída de ``pdbedit -L -v`` -> [(usuário, habilitado)].
QList<QPair<QString, bool>> parseUsers(const QString &pdbeditOutput);

class UsersPage : public Page {
    Q_OBJECT
public:
    explicit UsersPage(MainWindow &win);
    void refresh() override;
    void fill(const QString &error = {});

    QLineEdit *filter;
    TableBox *table;

private:
    QString selected();
    void add();
    void password();
    void setEnabled(bool enabled);
    void remove();
    MainWindow &m_win;
    QList<QPair<QString, bool>> m_users;
    QTimer *m_debounce;
};

}  // namespace sm::ui
