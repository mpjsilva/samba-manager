#include "ui/mainwindow.h"

#include "core/config.h"
#include "ui/dialogs.h"
#include "ui/pages/backupspage.h"
#include "ui/pages/globalpage.h"
#include "ui/pages/servicepage.h"
#include "ui/pages/sharespage.h"
#include "ui/pages/statuspage.h"
#include "ui/pages/userspage.h"
#include "ui/widgets.h"

#include <QButtonGroup>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace sm::ui {

namespace {

struct Outcome {
    QJsonValue value;
    QString error;
    bool failed = false;
};

const char *const kNavLabels[MainWindow::PageCount] = {
    "Serviço", "Compartilhamentos", "Usuários", "Global", "Conexões", "Backups e log",
};

}  // namespace

MainWindow::MainWindow(Backend &backend, QWidget *parent) : QMainWindow(parent), m_backend(backend)
{
    setWindowTitle(QStringLiteral("Samba Manager"));
    resize(1160, 740);
    setMinimumSize(1000, 660);

    auto *central = new QWidget;
    auto *row = new QHBoxLayout(central);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(buildSidebar());

    auto *content = new QWidget;
    auto *col = new QVBoxLayout(content);
    col->setContentsMargins(36, 12, 36, 28);
    col->setSpacing(14);
    m_busyBar = new BusyBar;
    col->addWidget(m_busyBar);
    m_stack = new QStackedWidget;
    col->addWidget(m_stack, 1);
    row->addWidget(content, 1);
    setCentralWidget(central);
    m_toast = new Toast(content);

    m_pages[Service] = new ServicePage(*this);
    m_pages[Shares] = new SharesPage(*this);
    m_pages[Users] = new UsersPage(*this);
    m_pages[Global] = new GlobalPage(*this);
    m_pages[Status] = new StatusPage(*this);
    m_pages[Backups] = new BackupsPage(*this);
    for (Page *p : m_pages)
        m_stack->addWidget(p);

    for (int i = 0; i < PageCount; ++i)
        connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_1 + i)), this), &QShortcut::activated,
                this, [this, i] { showPage(PageId(i)); });
    connect(new QShortcut(QKeySequence(Qt::Key_F5), this), &QShortcut::activated, this,
            [this] { refresh(m_current); });
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), this), &QShortcut::activated, this,
            &QWidget::close);
    showPage(Service);
}

MainWindow::~MainWindow() = default;

QWidget *MainWindow::buildSidebar()
{
    auto *sb = new QWidget;
    sb->setObjectName(QStringLiteral("sidebar"));
    sb->setAttribute(Qt::WA_StyledBackground);
    sb->setFixedWidth(252);
    auto *lay = new QVBoxLayout(sb);
    lay->setContentsMargins(18, 28, 18, 22);
    lay->setSpacing(2);
    auto *brand = new QVBoxLayout;
    brand->setContentsMargins(6, 0, 0, 24);
    brand->setSpacing(2);
    brand->addWidget(label(QStringLiteral("Samba Manager"), QStringLiteral("h1")));
    brand->addWidget(label(QStringLiteral("Fedora · administrador"), QStringLiteral("small")));
    lay->addLayout(brand);
    auto *group = new QButtonGroup(this);
    for (int i = 0; i < PageCount; ++i) {
        auto *b = new QPushButton(QString::fromUtf8(kNavLabels[i]));
        b->setObjectName(QStringLiteral("nav"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(QStringLiteral("Ctrl+%1").arg(i + 1));
        group->addButton(b, i);
        lay->addWidget(b);
        m_nav[i] = b;
        connect(b, &QPushButton::clicked, this, [this, i] { showPage(PageId(i)); });
    }
    lay->addStretch(1);
    QLabel *ver = label(QStringLiteral("v%1").arg(QLatin1String(config::kAppVersion)), QStringLiteral("small"));
    ver->setContentsMargins(6, 0, 0, 0);
    lay->addWidget(ver);
    return sb;
}

void MainWindow::showPage(PageId id)
{
    m_current = id;
    m_stack->setCurrentWidget(m_pages[id]);
    m_nav[id]->setChecked(true);
    m_pages[id]->refresh();
}

void MainWindow::refresh(PageId id) { m_pages[id]->refresh(); }

void MainWindow::bg(Work work, Done done, bool busy)
{
    if (busy) {
        if (m_busy) {
            toast(QStringLiteral("Aguarde a operação atual terminar."), QStringLiteral("warning"));
            return;
        }
        m_busy = true;
        m_busyBar->start();
    }
    ++m_pending;
    auto *watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, done, busy] {
        watcher->deleteLater();
        --m_pending;
        if (busy) {
            m_busy = false;
            m_busyBar->stop();
        }
        const Outcome out = watcher->result();
        if (out.failed)
            message(this, QStringLiteral("Não foi possível concluir"), out.error);
        else if (done)
            done(out.value);
    });
    watcher->setFuture(QtConcurrent::run([work = std::move(work)]() -> Outcome {
        try {
            return {work(), {}, false};
        } catch (const std::exception &e) {
            return {{}, QString::fromUtf8(e.what()), true};
        } catch (...) {
            return {{}, QStringLiteral("erro desconhecido"), true};
        }
    }));
}

void MainWindow::call(const QString &op, const QJsonObject &args, Done done, bool busy)
{
    bg([this, op, args] { return m_backend.call(op, args); }, std::move(done), busy);
}

void MainWindow::toast(const QString &text, const QString &kind, int ms) { m_toast->popup(text, kind, ms); }

bool MainWindow::showResult(const QJsonValue &res, const QString &okMsg)
{
    const QJsonArray r = res.toArray();
    const int rc = r.at(0).toInt(-1);
    if (rc == 0) {
        toast(okMsg);
        return true;
    }
    const QString out = r.at(1).toString();
    message(this, QStringLiteral("Não foi possível concluir"),
            out.isEmpty() ? QStringLiteral("O comando falhou (código %1).").arg(rc) : out);
    return false;
}

}  // namespace sm::ui
