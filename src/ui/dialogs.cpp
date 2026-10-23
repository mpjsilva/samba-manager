#include "ui/dialogs.h"

#include "core/selinux.h"
#include "ui/widgets.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace sm::ui {

namespace {

const QStringList kShareFields = {"path", "comment", "valid users", "read only",
                                  "guest ok", "browseable", "writable", "writeable"};

}  // namespace

// ---- FormDialog ----

FormDialog::FormDialog(QWidget *parent, const QString &title, const QString &subtitle,
                       const QString &okText, const QString &cancelText, bool danger, int width)
    : QDialog(parent), m_body(new QVBoxLayout)
{
    setWindowTitle(title);
    setModal(true);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 22);
    root->setSpacing(0);
    root->addWidget(label(title, QStringLiteral("h1")));
    if (!subtitle.isEmpty()) {
        QLabel *sub = label(subtitle, QStringLiteral("muted"));
        sub->setWordWrap(true);
        root->addSpacing(3);
        root->addWidget(sub);
    }
    m_body->setContentsMargins(0, 8, 0, 0);
    m_body->setSpacing(0);
    root->addLayout(m_body);
    root->addSpacing(22);
    auto *foot = new QHBoxLayout;
    foot->addStretch(1);
    if (!cancelText.isEmpty()) {
        QPushButton *cancel = button(cancelText, QStringLiteral("secondary"), QStringLiteral("ghost"));
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        foot->addWidget(cancel);
        foot->addSpacing(8);
    }
    QPushButton *ok = button(okText, danger ? QStringLiteral("danger") : QStringLiteral("primary"));
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, &FormDialog::accept);
    foot->addWidget(ok);
    root->addLayout(foot);
    setMinimumWidth(width + 56);
}

QLineEdit *FormDialog::addEntry(const QString &labelText, const QString &hint, bool password)
{
    m_body->addSpacing(12);
    m_body->addWidget(label(labelText, QStringLiteral("bold")));
    m_body->addSpacing(5);
    auto *e = new QLineEdit;
    if (password)
        e->setEchoMode(QLineEdit::Password);
    m_body->addWidget(e);
    if (!hint.isEmpty()) {
        m_body->addSpacing(4);
        m_body->addWidget(label(hint, QStringLiteral("small")));
    }
    return e;
}

QLineEdit *FormDialog::addEntryWithButton(const QString &labelText, const QString &buttonText,
                                          QPushButton **btn, const QString &hint)
{
    m_body->addSpacing(12);
    m_body->addWidget(label(labelText, QStringLiteral("bold")));
    m_body->addSpacing(5);
    auto *row = new QHBoxLayout;
    auto *e = new QLineEdit;
    row->addWidget(e, 1);
    *btn = smallButton(buttonText, QStringLiteral("secondary"), QStringLiteral("outline"));
    row->addWidget(*btn);
    m_body->addLayout(row);
    if (!hint.isEmpty()) {
        m_body->addSpacing(4);
        m_body->addWidget(label(hint, QStringLiteral("small")));
    }
    return e;
}

QCheckBox *FormDialog::addToggle(const QString &text, bool checked)
{
    auto *c = new QCheckBox(text);
    c->setChecked(checked);
    m_body->addSpacing(8);
    m_body->addWidget(c);
    return c;
}

void FormDialog::accept()
{
    if (validate())
        QDialog::accept();
}

// ---- mensagens ----

namespace {

class MessageDialog : public FormDialog {
public:
    MessageDialog(QWidget *parent, const QString &title, const QString &text, const QString &okText,
                  const QString &cancelText, bool danger)
        : FormDialog(parent, title, {}, okText, cancelText, danger)
    {
        body()->addSpacing(10);
        if (text.size() > 420 || text.count('\n') > 5) {
            auto *t = new TextView;
            t->setText(text);
            t->setMinimumSize(620, 260);
            body()->addWidget(t);
        } else {
            QLabel *l = label(text);
            l->setWordWrap(true);
            l->setTextInteractionFlags(Qt::TextSelectableByMouse);
            body()->addWidget(l);
        }
    }
};

}  // namespace

void message(QWidget *parent, const QString &title, const QString &text)
{
    MessageDialog(parent, title, text, QStringLiteral("OK"), {}, false).exec();
}

bool confirm(QWidget *parent, const QString &title, const QString &text, const QString &okText, bool danger)
{
    return MessageDialog(parent, title, text, okText, QStringLiteral("Cancelar"), danger).exec() ==
           QDialog::Accepted;
}

void showTextWindow(QWidget *parent, const QString &title, const QString &text)
{
    auto *win = new QDialog(parent);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->setWindowTitle(title);
    win->resize(820, 520);
    auto *lay = new QVBoxLayout(win);
    lay->setContentsMargins(16, 16, 16, 16);
    auto *t = new TextView;
    t->setText(text);
    lay->addWidget(t);
    win->show();
}

// ---- ShareDialog ----

ShareDialog::ShareDialog(QWidget *parent, const QStringList &existing, const QString &shareName,
                         const Params &p)
    : FormDialog(parent, shareName.isEmpty() ? QStringLiteral("Novo compartilhamento")
                                             : QStringLiteral("Editar compartilhamento"),
                 QStringLiteral("Pasta publicada na rede via SMB."), QStringLiteral("Salvar"),
                 QStringLiteral("Cancelar"), false, 540),
      m_editing(!shareName.isEmpty()), m_existing(existing)
{
    for (const auto &[k, v] : p.items())
        if (!kShareFields.contains(k))
            m_extra.set(k, v);

    nameEdit = addEntry(QStringLiteral("Nome"));
    nameEdit->setText(shareName);
    nameEdit->setEnabled(!m_editing);
    QPushButton *browseBtn = nullptr;
    pathEdit = addEntryWithButton(QStringLiteral("Pasta"), QStringLiteral("Procurar..."), &browseBtn,
                                  QStringLiteral("Caminho absoluto, ex.: /srv/samba/publico"));
    pathEdit->setText(p.value(QStringLiteral("path")));
    connect(browseBtn, &QPushButton::clicked, this, &ShareDialog::browse);
    commentEdit = addEntry(QStringLiteral("Comentário"));
    commentEdit->setText(p.value(QStringLiteral("comment")));
    usersEdit = addEntry(QStringLiteral("Usuários válidos"),
                         QStringLiteral("Separe por vírgula; use @grupo para grupos. Vazio = todos."));
    usersEdit->setText(p.value(QStringLiteral("valid users")));
    body()->addSpacing(6);
    readOnly = addToggle(QStringLiteral("Somente leitura"), isYes(p.value(QStringLiteral("read only"), "no")));
    guest = addToggle(QStringLiteral("Permitir convidados (guest)"), isYes(p.value(QStringLiteral("guest ok"), "no")));
    browseable = addToggle(QStringLiteral("Visível na rede (browseable)"),
                           isYes(p.value(QStringLiteral("browseable"), "yes")));
    selinuxBox = addToggle(QStringLiteral("Criar pasta e ajustar SELinux (samba_share_t)"), true);
    (m_editing ? pathEdit : nameEdit)->setFocus();
}

void ShareDialog::browse()
{
    QString start = pathEdit->text().trimmed();
    if (start.isEmpty() || !QFileInfo(start).isDir())
        start = QDir::homePath();  // a interface roda como o usuário: nunca começa em "/"
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Selecione a pasta para compartilhamento"), start);
    if (!dir.isEmpty())
        pathEdit->setText(dir);
}

QString ShareDialog::name() const { return nameEdit->text().trimmed(); }
bool ShareDialog::selinux() const { return selinuxBox->isChecked(); }

Params ShareDialog::params() const
{
    Params p;
    auto yn = [](const QCheckBox *c) { return c->isChecked() ? QStringLiteral("yes") : QStringLiteral("no"); };
    p.set(QStringLiteral("comment"), commentEdit->text().trimmed());
    p.set(QStringLiteral("path"), pathEdit->text().trimmed());
    p.set(QStringLiteral("valid users"), usersEdit->text().trimmed());
    p.set(QStringLiteral("browseable"), yn(browseable));
    p.set(QStringLiteral("read only"), yn(readOnly));
    p.set(QStringLiteral("guest ok"), yn(guest));
    for (const auto &[k, v] : m_extra.items())
        p.set(k, v);
    return p;
}

QPair<QString, QString> ShareDialog::problem() const
{
    const QString path = pathEdit->text().trimmed();
    const QString err = shareNameError(name(), m_existing, !m_editing);
    if (!err.isEmpty())
        return {QStringLiteral("Nome inválido"), err};
    for (const QString &v : {path, commentEdit->text(), usersEdit->text()})
        if (hasControlChars(v))
            return {QStringLiteral("Valor inválido"), QStringLiteral("Os campos não podem conter quebras de linha.")};
    if (!path.startsWith('/'))
        return {QStringLiteral("Caminho inválido"), QStringLiteral("Informe um caminho absoluto, começando por /.")};
    if (selinux()) {
        const QString reason = labelBlockReason(path);
        if (!reason.isEmpty())
            return {QStringLiteral("Pasta não pode ser rotulada"),
                    QStringLiteral("%1: %2\n\nPara compartilhar sem rotular, desmarque “Criar pasta e "
                                   "ajustar SELinux”.")
                        .arg(path, reason)};
    }
    return {};
}

bool ShareDialog::validate()
{
    const auto [title, text] = problem();
    if (title.isEmpty())
        return true;
    message(this, title, text);
    return false;
}

// ---- UserDialog / PasswordDialog ----

UserDialog::UserDialog(QWidget *parent)
    : FormDialog(parent, QStringLiteral("Novo usuário"),
                 QStringLiteral("Se o usuário Linux não existir, será criado sem shell e sem diretório pessoal."),
                 QStringLiteral("Continuar"))
{
    edit = addEntry(QStringLiteral("Nome de usuário"), QStringLiteral("Minúsculas, números, _ e -"));
}

QString UserDialog::user() const { return edit->text().trimmed(); }

bool UserDialog::isValidUser(const QString &user)
{
    static const QRegularExpression re(QStringLiteral("^[a-z_][a-z0-9_-]{0,31}$"));
    return re.match(user).hasMatch();
}

bool UserDialog::validate()
{
    if (isValidUser(user()))
        return true;
    message(this, QStringLiteral("Nome inválido"),
            QStringLiteral("Use apenas minúsculas, números, _ e - (até 32 caracteres)."));
    return false;
}

PasswordDialog::PasswordDialog(QWidget *parent, const QString &title)
    : FormDialog(parent, title, QStringLiteral("Esta é a senha usada para acessar os compartilhamentos."))
{
    first = addEntry(QStringLiteral("Senha"), {}, true);
    second = addEntry(QStringLiteral("Confirmar senha"), {}, true);
}

QString PasswordDialog::password() const { return first->text(); }

bool PasswordDialog::validate()
{
    if (first->text().isEmpty() || first->text() != second->text()) {
        message(this, QStringLiteral("Senhas diferentes"), QStringLiteral("As senhas não conferem."));
        return false;
    }
    if (hasControlChars(first->text())) {
        message(this, QStringLiteral("Senha inválida"), QStringLiteral("A senha não pode conter quebras de linha."));
        return false;
    }
    return true;
}

}  // namespace sm::ui
