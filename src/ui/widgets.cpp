#include "ui/widgets.h"

#include "ui/theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPushButton>
#include <QStackedLayout>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace sm::ui {

void setProp(QWidget *w, const char *name, const QVariant &value)
{
    w->setProperty(name, value);
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

QLabel *label(const QString &text, const QString &role, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    if (!role.isEmpty())
        l->setProperty("role", role);
    return l;
}

QPushButton *button(const QString &text, const QString &kind, const QString &variant, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setProperty("kind", kind);
    b->setProperty("variant", variant);
    b->setCursor(Qt::PointingHandCursor);
    b->setAutoDefault(false);
    return b;
}

QPushButton *smallButton(const QString &text, const QString &kind, const QString &variant, QWidget *parent)
{
    QPushButton *b = button(text, kind, variant, parent);
    b->setProperty("size", "small");
    return b;
}

QFrame *separator(QWidget *parent)
{
    auto *f = new QFrame(parent);
    f->setObjectName(QStringLiteral("separator"));
    f->setFrameShape(QFrame::NoFrame);
    return f;
}

bool isYes(const QString &value)
{
    const QString v = value.trimmed().toLower();
    return v == QLatin1String("yes") || v == QLatin1String("true") || v == QLatin1String("1");
}

// ---- Card / Pill ----

Card::Card(QWidget *parent) : QFrame(parent), m_box(new QVBoxLayout(this))
{
    setObjectName(QStringLiteral("card"));
    setAttribute(Qt::WA_StyledBackground);
    m_box->setContentsMargins(24, 22, 24, 22);
    m_box->setSpacing(0);
}

Pill::Pill(QWidget *parent) : QLabel(parent)
{
    QFont f = font();
    f.setPointSizeF(9);
    f.setWeight(QFont::DemiBold);
    setFont(f);
    setContentsMargins(12, 3, 12, 3);
    setAlignment(Qt::AlignCenter);
    set(QStringLiteral("…"), QStringLiteral("muted"));
}

void Pill::set(const QString &text, const QString &kind)
{
    m_kind = kind;
    setText(text);
    update();
}

void Pill::paintEvent(QPaintEvent *)
{
    const bool muted = m_kind == QLatin1String("muted");
    QColor fill = muted ? QColor(255, 255, 255) : theme::color(m_kind);
    fill.setAlphaF(muted ? 0.08 : 0.18);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
    p.setPen(muted ? theme::muted : theme::ink(theme::color(m_kind)));
    p.drawText(contentsRect(), Qt::AlignCenter, text());
}

// ---- Page ----

Page::Page(const QString &title, const QString &subtitle, QWidget *parent)
    : QWidget(parent), m_actions(new QHBoxLayout), m_body(new QVBoxLayout)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(20);
    auto *head = new QHBoxLayout;
    auto *left = new QVBoxLayout;
    left->setSpacing(2);
    left->addWidget(label(title, QStringLiteral("title")));
    left->addWidget(label(subtitle, QStringLiteral("muted")));
    head->addLayout(left, 1);
    m_actions->setSpacing(10);
    head->addLayout(m_actions);
    head->setAlignment(m_actions, Qt::AlignRight | Qt::AlignVCenter);
    root->addLayout(head);
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(16);
    root->addLayout(m_body, 1);
}

// ---- TableBox ----

TableBox::TableBox(const QList<Column> &columns, QWidget *parent)
    : QWidget(parent), m_tree(new QTreeWidget), m_empty(label({}, QStringLiteral("empty"))),
      m_stack(new QStackedLayout(this))
{
    m_stack->setContentsMargins(0, 0, 0, 0);
    QStringList titles;
    for (const Column &c : columns)
        titles << c.title;
    m_tree->setHeaderLabels(titles);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    for (int i = 0; i < columns.size(); ++i) {
        m_tree->header()->setSectionResizeMode(i, columns[i].stretch ? QHeaderView::Stretch
                                                                      : QHeaderView::Interactive);
        m_tree->header()->resizeSection(i, columns[i].width);
    }
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    // A mensagem fica sobre um fundo igual ao da tabela, para o card não "furar".
    auto *emptyBox = new QFrame;
    emptyBox->setObjectName(QStringLiteral("emptyBox"));
    emptyBox->setStyleSheet(QStringLiteral("#emptyBox { background: #2f2f2f; border: 1px solid #3a3a3a;"
                                           " border-radius: 10px; }"));
    auto *eb = new QVBoxLayout(emptyBox);
    eb->addWidget(m_empty);
    m_stack->addWidget(m_tree);
    m_stack->addWidget(emptyBox);
}

void TableBox::setEmpty(const QString &text)
{
    m_empty->setText(text);
    m_stack->setCurrentIndex(text.isEmpty() ? 0 : 1);
}

QString TableBox::emptyText() const { return m_stack->currentIndex() == 1 ? m_empty->text() : QString(); }

QString TableBox::selectedKey() const
{
    const auto sel = m_tree->selectedItems();
    return sel.isEmpty() ? QString() : sel.first()->data(0, Qt::UserRole).toString();
}

// ---- TextView ----

TextView::TextView(QWidget *parent) : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
}

void TextView::setText(const QString &text, bool scrollToEnd)
{
    setPlainText(text);
    if (scrollToEnd)
        moveCursor(QTextCursor::End);
}

// ---- Toast ----

Toast::Toast(QWidget *parent) : QLabel(parent), m_timer(new QTimer(this))
{
    setObjectName(QStringLiteral("toast"));
    setWordWrap(true);
    setMaximumWidth(460);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &QWidget::hide);
    parent->installEventFilter(this);
    hide();
}

void Toast::popup(const QString &text, const QString &kind, int ms)
{
    setText(text);
    setProp(this, "kind", kind);
    adjustSize();
    reposition();
    show();
    raise();
    m_timer->start(ms);
}

bool Toast::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == parent() && ev->type() == QEvent::Resize && isVisible())
        reposition();
    return QLabel::eventFilter(obj, ev);
}

void Toast::reposition()
{
    const QWidget *p = parentWidget();
    move(p->width() - width() - 24, p->height() - height() - 22);
}

// ---- BusyBar ----

BusyBar::BusyBar(QWidget *parent) : QProgressBar(parent)
{
    setObjectName(QStringLiteral("busy"));
    setTextVisible(false);
    setFixedHeight(3);
    setRange(0, 1);
    setValue(0);
    QSizePolicy sp = sizePolicy();
    sp.setRetainSizeWhenHidden(true);
    setSizePolicy(sp);
    hide();
}

void BusyBar::start()
{
    setRange(0, 0);
    show();
}

void BusyBar::stop()
{
    setRange(0, 1);
    hide();
}

}  // namespace sm::ui
