// Página Backups e log: versões, comparar, restaurar, backup manual.
#pragma once

#include "ui/widgets.h"

class QComboBox;

namespace sm::ui {

class MainWindow;

class BackupsPage : public Page {
    Q_OBJECT
public:
    explicit BackupsPage(MainWindow &win);
    void refresh() override;
    QString path() const;

    QComboBox *combo;
    TableBox *table;
    TextView *log;

private:
    QString selected();
    void view();
    void compare();
    void manual();
    void restore();
    void doRestore(const QString &path, const QString &name, bool force);
    MainWindow &m_win;
};

}  // namespace sm::ui
