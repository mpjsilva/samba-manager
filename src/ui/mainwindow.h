// Janela principal: sidebar, navegação, tarefas em segundo plano e toasts.
#pragma once

#include "core/protocol.h"

#include <QJsonValue>
#include <QMainWindow>

#include <functional>

class QPushButton;
class QStackedWidget;

namespace sm::ui {

class BusyBar;
class Page;
class Toast;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    enum PageId { Service, Shares, Users, Global, Status, Backups, PageCount };

    explicit MainWindow(Backend &backend, QWidget *parent = nullptr);
    ~MainWindow() override;

    Backend &backend() { return m_backend; }

    using Work = std::function<QJsonValue()>;
    using Done = std::function<void(const QJsonValue &)>;
    //! Executa ``work`` numa thread do pool; ``done`` roda na thread da UI.
    //! Com ``busy``, mostra a barra e recusa outra operação enquanto esta roda
    //! (evita duas gravações disputando o smb.conf).
    void bg(Work work, Done done = {}, bool busy = true);
    //! Atalho: bg() de uma operação do auxiliar.
    void call(const QString &op, const QJsonObject &args, Done done = {}, bool busy = true);

    void toast(const QString &text, const QString &kind = QStringLiteral("success"), int ms = 3400);
    //! Resultado [rc, saída] de um comando: toast em sucesso, diálogo em erro.
    bool showResult(const QJsonValue &res, const QString &okMsg);

    void showPage(PageId id);
    PageId currentPage() const { return m_current; }
    Page *page(PageId id) const { return m_pages[id]; }
    void refresh(PageId id);
    bool isBusy() const { return m_busy; }
    //! Tarefas ainda não concluídas (usado pelos testes).
    int pending() const { return m_pending; }

private:
    QWidget *buildSidebar();

    Backend &m_backend;
    QStackedWidget *m_stack = nullptr;
    BusyBar *m_busyBar = nullptr;
    Toast *m_toast = nullptr;
    Page *m_pages[PageCount] = {};
    QPushButton *m_nav[PageCount] = {};
    PageId m_current = Service;
    bool m_busy = false;
    int m_pending = 0;
};

}  // namespace sm::ui
