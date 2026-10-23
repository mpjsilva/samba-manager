#include "ui/theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

namespace sm::ui::theme {

namespace {

const QStringList kKinds = {"primary", "secondary", "success", "info", "warning", "danger"};

QString hex(const QColor &c) { return c.name(QColor::HexRgb); }

QString rgba(const QColor &c, double alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

QString firstAvailable(const QStringList &wanted, const QString &fallback)
{
    const QStringList families = QFontDatabase::families();
    for (const QString &f : wanted)
        if (families.contains(f))
            return f;
    return fallback;
}

QString buttonRules()
{
    QString qss;
    for (const QString &k : kKinds) {
        const QColor c = color(k);
        const QColor text = k == QLatin1String("secondary") ? QColor("#e6e6e6") : ink(c);
        qss += QStringLiteral(
                   "QPushButton[kind=\"%1\"] { background: %2; }"
                   "QPushButton[kind=\"%1\"]:hover { background: %3; }"
                   "QPushButton[kind=\"%1\"]:pressed { background: %4; }"
                   "QPushButton[kind=\"%1\"][variant=\"outline\"] { background: transparent; "
                   "  border: 1px solid %5; color: %6; }"
                   "QPushButton[kind=\"%1\"][variant=\"outline\"]:hover { background: %7; }"
                   "QPushButton[kind=\"%1\"][variant=\"ghost\"] { background: transparent; color: %6; }"
                   "QPushButton[kind=\"%1\"][variant=\"ghost\"]:hover { background: %7; }"
                   "QLabel#toast[kind=\"%1\"] { background: %8; border: 1px solid %5; }")
                   .arg(k, hex(c), hex(c.lighter(115)), hex(c.darker(115)),
                        hex(k == QLatin1String("secondary") ? QColor("#5a5a5a") : c), hex(text),
                        rgba(c, 0.16), hex(blend(bg, c, 0.35)));
    }
    return qss;
}

}  // namespace

QColor color(const QString &kind)
{
    static const QHash<QString, QColor> map = {
        {"primary", primary}, {"secondary", secondary}, {"success", success},
        {"info", info},       {"warning", warning},     {"danger", danger},
    };
    return map.value(kind, muted);
}

QColor blend(const QColor &a, const QColor &b, double t)
{
    auto mix = [t](int x, int y) { return qRound(x * (1 - t) + y * t); };
    return QColor(mix(a.red(), b.red()), mix(a.green(), b.green()), mix(a.blue(), b.blue()));
}

QColor ink(const QColor &c) { return blend(c, fg, 0.25); }

QString monoFamily()
{
    return firstAvailable({"JetBrains Mono", "Cascadia Mono", "Noto Sans Mono", "DejaVu Sans Mono",
                           "Liberation Mono"},
                          QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
}

void apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette p;
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, fg);
    p.setColor(QPalette::Base, input);
    p.setColor(QPalette::AlternateBase, card);
    p.setColor(QPalette::Text, fg);
    p.setColor(QPalette::Button, secondary);
    p.setColor(QPalette::ButtonText, fg);
    p.setColor(QPalette::Highlight, primary);
    p.setColor(QPalette::HighlightedText, fg);
    p.setColor(QPalette::ToolTipBase, card);
    p.setColor(QPalette::ToolTipText, fg);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::Link, ink(info));
    p.setColor(QPalette::Disabled, QPalette::Text, blend(muted, bg, 0.3));
    p.setColor(QPalette::Disabled, QPalette::WindowText, blend(muted, bg, 0.3));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, blend(muted, bg, 0.3));
    app.setPalette(p);

    QFont font(firstAvailable({"Inter", "Cantarell", "Noto Sans", "Open Sans", "DejaVu Sans"},
                              app.font().family()));
    font.setPointSize(10);
    app.setFont(font);

    const QString mono = monoFamily();
    app.setStyleSheet(
        QStringLiteral(
            "QToolTip { border: 1px solid %4; padding: 4px 8px; }"
            "#sidebar { background: %1; }"
            "#card { background: %2; border: 1px solid %4; border-radius: 14px; }"
            "QLabel[role=\"title\"] { font-size: 22pt; font-weight: 700; }"
            "QLabel[role=\"h1\"] { font-size: 14pt; font-weight: 700; }"
            "QLabel[role=\"h2\"] { font-size: 12pt; font-weight: 700; }"
            "QLabel[role=\"bold\"] { font-weight: 700; }"
            "QLabel[role=\"muted\"] { color: %5; }"
            "QLabel[role=\"small\"] { color: %5; font-size: 9pt; }"
            "QLabel[role=\"empty\"] { color: %5; font-size: 11pt; }"
            "QLabel#toast { border-radius: 10px; padding: 12px 18px; font-weight: 600; }"
            "QPushButton { border: 1px solid transparent; border-radius: 9px; padding: 8px 16px;"
            "  font-weight: 600; color: %6; background: %7; }"
            "QPushButton:disabled { color: %5; }"
            "QPushButton:focus { outline: none; }"
            "QPushButton[size=\"small\"] { padding: 5px 12px; }"
            "QPushButton#nav { text-align: left; padding: 10px 14px; border-radius: 10px;"
            "  background: transparent; color: %8; font-size: 11pt; font-weight: 500; }"
            "QPushButton#nav:hover { background: rgba(255,255,255,0.05); color: %6; }"
            "QPushButton#nav:checked { background: %9; color: %6; font-weight: 700; }"
            "QLineEdit, QComboBox { background: %3; border: 1px solid %10; border-radius: 8px;"
            "  padding: 7px 10px; selection-background-color: %11; }"
            "QLineEdit:focus, QComboBox:focus { border: 1px solid %11; }"
            "QLineEdit:disabled { color: %5; background: %2; }"
            "QComboBox::drop-down { border: none; width: 26px; }"
            "QComboBox QAbstractItemView { background: %3; border: 1px solid %4;"
            "  selection-background-color: %11; outline: none; }"
            "QCheckBox { spacing: 10px; padding: 3px 0; }"
            "QTreeWidget { background: %3; border: 1px solid %4; border-radius: 10px;"
            "  outline: none; padding: 2px; }"
            "QTreeWidget::item { height: 38px; padding-left: 8px; border: none; }"
            "QTreeWidget::item:hover { background: rgba(255,255,255,0.04); }"
            "QTreeWidget::item:selected { background: %11; color: %6; }"
            "QHeaderView { background: transparent; }"
            "QHeaderView::section { background: %3; color: %8; border: none;"
            "  border-bottom: 1px solid %4; padding: 8px 8px 8px 11px; font-weight: 700; }"
            "QPlainTextEdit { background: #1b1b1b; border: 1px solid %4; border-radius: 10px;"
            "  padding: 8px; font-family: \"%12\"; font-size: 9pt;"
            "  selection-background-color: %11; }"
            "QFrame#separator { background: %4; max-height: 1px; min-height: 1px; border: none; }"
            "QProgressBar#busy { background: transparent; border: none; max-height: 3px; }"
            "QProgressBar#busy::chunk { background: %11; border-radius: 1px; }"
            "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
            "QScrollBar::handle:vertical { background: #4a4a4a; border-radius: 4px; min-height: 30px; }"
            "QScrollBar::handle:vertical:hover { background: #5a5a5a; }"
            "QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }"
            "QScrollBar::handle:horizontal { background: #4a4a4a; border-radius: 4px; min-width: 30px; }"
            "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }"
            "QScrollBar::add-page, QScrollBar::sub-page { background: none; }")
            .arg(hex(side), hex(card), hex(input), hex(border), hex(muted), hex(fg), hex(secondary),
                 "#cfcfcf", rgba(primary, 0.55), "#474747", hex(primary))
            .arg(mono) +
        buttonRules());
}

}  // namespace sm::ui::theme
