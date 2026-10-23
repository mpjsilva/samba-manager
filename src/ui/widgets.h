// Blocos de interface reutilizados pelas páginas.
#pragma once

#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QWidget>

class QBoxLayout;
class QHBoxLayout;
class QPushButton;
class QStackedLayout;
class QTimer;
class QTreeWidget;
class QVBoxLayout;

namespace sm::ui {

//! Troca uma propriedade dinâmica e re-aplica a folha de estilo no widget.
void setProp(QWidget *w, const char *name, const QVariant &value);

//! Rótulo com papel de estilo: title, h1, h2, bold, muted, small (ou vazio).
QLabel *label(const QString &text, const QString &role = {}, QWidget *parent = nullptr);
//! Botão. kind: primary/secondary/success/info/warning/danger;
//! variant: solid/outline/ghost.
QPushButton *button(const QString &text, const QString &kind = QStringLiteral("primary"),
                    const QString &variant = QStringLiteral("solid"), QWidget *parent = nullptr);
//! Mesmo botão, mais baixo (barras de ferramentas das tabelas).
QPushButton *smallButton(const QString &text, const QString &kind, const QString &variant,
                         QWidget *parent = nullptr);
QFrame *separator(QWidget *parent = nullptr);

//! Superfície arredondada; o conteúdo vai em box().
class Card : public QFrame {
public:
    explicit Card(QWidget *parent = nullptr);
    QVBoxLayout *box() const { return m_box; }

private:
    QVBoxLayout *m_box;
};

//! Etiqueta de estado arredondada (desenhada à mão: a folha de estilo não
//! arredonda o fundo de um QLabel de forma confiável).
class Pill : public QLabel {
public:
    explicit Pill(QWidget *parent = nullptr);
    void set(const QString &text, const QString &kind);
    QString kind() const { return m_kind; }

protected:
    void paintEvent(QPaintEvent *ev) override;

private:
    QString m_kind;
};

//! Tela com título, subtítulo, ações à direita e corpo.
class Page : public QWidget {
    Q_OBJECT
public:
    Page(const QString &title, const QString &subtitle, QWidget *parent = nullptr);
    QHBoxLayout *actions() const { return m_actions; }
    QVBoxLayout *body() const { return m_body; }
    //! Recarrega os dados (ao abrir a página e com F5).
    virtual void refresh() {}

private:
    QHBoxLayout *m_actions;
    QVBoxLayout *m_body;
};

struct Column {
    QString title;
    int width;
    bool stretch;
};

//! Tabela padronizada com mensagem centralizada quando vazia.
class TableBox : public QWidget {
public:
    TableBox(const QList<Column> &columns, QWidget *parent = nullptr);
    QTreeWidget *tree() const { return m_tree; }
    //! Mostra ``text`` no lugar da tabela; vazio volta a mostrar a tabela.
    void setEmpty(const QString &text);
    QString emptyText() const;
    //! Texto da coluna 0 da linha selecionada, ou vazio.
    QString selectedKey() const;

private:
    QTreeWidget *m_tree;
    QLabel *m_empty;
    QStackedLayout *m_stack;
};

//! Texto monoespaçado somente leitura.
class TextView : public QPlainTextEdit {
public:
    explicit TextView(QWidget *parent = nullptr);
    void setText(const QString &text, bool scrollToEnd = false);
};

//! Aviso temporário no canto inferior direito do pai.
class Toast : public QLabel {
public:
    explicit Toast(QWidget *parent);
    void popup(const QString &text, const QString &kind, int ms);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void reposition();
    QTimer *m_timer;
};

//! Barra fina indeterminada; ocupa sempre o mesmo espaço.
class BusyBar : public QProgressBar {
public:
    explicit BusyBar(QWidget *parent = nullptr);
    void start();
    void stop();
};

//! Valor booleano do smb.conf (yes/true/1).
bool isYes(const QString &value);

}  // namespace sm::ui
