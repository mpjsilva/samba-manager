// Página Global: workgroup, server string, netbios name.
#pragma once

#include "ui/widgets.h"

#include <QMap>

class QLineEdit;

namespace sm::ui {

class MainWindow;

class GlobalPage : public Page {
    Q_OBJECT
public:
    explicit GlobalPage(MainWindow &win);
    void refresh() override;
    QMap<QString, QLineEdit *> fields;

private:
    void save();
    MainWindow &m_win;
};

}  // namespace sm::ui
