// Samba Manager NG — interface. Roda como usuário comum e abre o auxiliar
// root (samba-manager-ng-helper) via pkexec uma única vez, no início.
#include "core/config.h"
#include "core/protocol.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QThreadPool>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("samba-manager-ng"));
    QApplication::setApplicationDisplayName(QStringLiteral("Samba Manager"));
    QApplication::setApplicationVersion(QString::fromLatin1(sm::config::kAppVersion));
    QApplication::setDesktopFileName(QStringLiteral("samba-manager-ng"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/samba-manager-ng.png")));
    sm::ui::theme::apply(app);

    QString error;
    std::unique_ptr<sm::Backend> backend = sm::connectBackend(QCoreApplication::applicationDirPath(), &error);
    if (!backend) {
        QMessageBox::critical(nullptr, QStringLiteral("Samba Manager"), error);
        return 1;
    }
    int rc = 0;
    {
        sm::ui::MainWindow win(*backend);
        win.show();
        rc = app.exec();
    }
    // Tarefas em andamento usam o backend: espera antes de fechá-lo.
    QThreadPool::globalInstance()->waitForDone();
    return rc;
}
