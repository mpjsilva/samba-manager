// Diálogos modais: base de formulário, mensagens e formulários específicos.
#pragma once

#include "core/smbconf.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace sm::ui {

//! Diálogo com título, subtítulo, corpo livre e botões no rodapé. O botão OK
//! chama validate(); só fecha se ela retornar true.
class FormDialog : public QDialog {
    Q_OBJECT
public:
    FormDialog(QWidget *parent, const QString &title, const QString &subtitle = {},
               const QString &okText = QStringLiteral("Salvar"),
               const QString &cancelText = QStringLiteral("Cancelar"), bool danger = false,
               int width = 520);
    QVBoxLayout *body() const { return m_body; }
    QLineEdit *addEntry(const QString &label, const QString &hint = {}, bool password = false);
    //! Campo com botão à direita (ex.: "Procurar...").
    QLineEdit *addEntryWithButton(const QString &label, const QString &buttonText, QPushButton **button,
                                  const QString &hint = {});
    QCheckBox *addToggle(const QString &text, bool checked);

    void accept() override;

protected:
    virtual bool validate() { return true; }

private:
    QVBoxLayout *m_body;
};

void message(QWidget *parent, const QString &title, const QString &text);
bool confirm(QWidget *parent, const QString &title, const QString &text,
             const QString &okText = QStringLiteral("Confirmar"), bool danger = false);
//! Janela não modal com texto monoespaçado (conteúdo de versões, diffs).
void showTextWindow(QWidget *parent, const QString &title, const QString &text);

class ShareDialog : public FormDialog {
    Q_OBJECT
public:
    //! ``name`` vazio = novo compartilhamento.
    ShareDialog(QWidget *parent, const QStringList &existing, const QString &name = {},
                const Params &params = {});
    QString name() const;
    //! Parâmetros na ordem de gravação (inclui os extras que a tela não edita).
    Params params() const;
    bool selinux() const;

    //! Motivo para não aceitar o formulário (título, texto), ou vazio.
    QPair<QString, QString> problem() const;

    QLineEdit *nameEdit, *pathEdit, *commentEdit, *usersEdit;
    QCheckBox *readOnly, *guest, *browseable, *selinuxBox;

protected:
    bool validate() override;

private:
    void browse();
    bool m_editing;
    QStringList m_existing;
    Params m_extra;
};

class UserDialog : public FormDialog {
    Q_OBJECT
public:
    explicit UserDialog(QWidget *parent);
    QString user() const;
    static bool isValidUser(const QString &user);
    QLineEdit *edit;

protected:
    bool validate() override;
};

class PasswordDialog : public FormDialog {
    Q_OBJECT
public:
    PasswordDialog(QWidget *parent, const QString &title);
    QString password() const;
    QLineEdit *first, *second;

protected:
    bool validate() override;
};

}  // namespace sm::ui
