// Página Conexões: saída do smbstatus em texto monoespaçado.
#pragma once

#include "ui/widgets.h"

namespace sm::ui {

class MainWindow;

class StatusPage : public Page {
    Q_OBJECT
public:
    explicit StatusPage(MainWindow &win);
    void refresh() override;
    TextView *text;

private:
    MainWindow &m_win;
};

}  // namespace sm::ui
