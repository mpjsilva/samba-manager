// Separação de privilégios: UI como usuário, auxiliar root via pkexec.
//
// A UI abre ``pkexec samba-manager-ng-helper`` uma única vez no início e
// conversa com ele por JSON, uma mensagem por linha, em stdin/stdout:
//
//     UI -> auxiliar   {"op": "user_add", "args": {"user": "joao", ...}}
//     auxiliar -> UI   {"ok": true, "result": ...} | {"ok": false, "error": "..."}
//
// O auxiliar só executa operações registradas em ops.cpp. Ao fechar a UI, o
// stdin dele chega ao fim e ele encerra. Rodando já como root (ex.: sudo), as
// operações são chamadas no próprio processo (LocalBackend).
#pragma once

#include "core/ops.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <sys/types.h>

namespace sm {

//! Falha reportada pelo auxiliar (ou perda da conexão com ele).
class BackendError : public std::runtime_error {
public:
    explicit BackendError(const QString &msg) : std::runtime_error(msg.toStdString()) {}
    QString message() const { return QString::fromStdString(what()); }
};

class Backend {
public:
    virtual ~Backend() = default;
    //! Executa ``op``. Lança BackendError em erro.
    virtual QJsonValue call(const QString &op, const QJsonObject &args = {}) = 0;
};

//! Explica por que o auxiliar não subiu, conforme os códigos do pkexec(1).
QString startFailureMessage(int rc, const QStringList &stderrLines);

//! Atende pedidos lidos de ``inFd`` até o fim, respondendo em ``outFd``.
void serveLoop(int inFd, int outFd, const OpMap &ops);
//! Ponto de entrada do auxiliar (deve rodar como root). Retorna o exit code.
int serveMain();

//! Fala com o auxiliar por pipes. Thread-safe: um pedido por vez.
class HelperClient : public Backend {
public:
    //! Inicia ``cmd`` e espera o aperto de mão. Lança BackendError se falhar.
    explicit HelperClient(const QStringList &cmd);
    ~HelperClient() override;
    QJsonValue call(const QString &op, const QJsonObject &args = {}) override;

private:
    QByteArray readLine();
    int finish();  // fecha o stdin do filho e colhe o código de saída
    void drainStderr(int fd);

    pid_t m_pid = -1;
    int m_in = -1, m_out = -1;
    QByteArray m_buf;
    std::mutex m_lock, m_tailLock;
    std::deque<QString> m_tail;
    std::thread m_stderrThread;
};

//! Mesmo contrato, executando no próprio processo (já root).
class LocalBackend : public Backend {
public:
    explicit LocalBackend(const OpMap &ops = operations()) : m_ops(ops) {}
    QJsonValue call(const QString &op, const QJsonObject &args = {}) override;

private:
    const OpMap &m_ops;
};

//! Comando que abre o auxiliar. Num AppImage o binário fica no mount FUSE do
//! usuário, que o root não lê: nesse caso o próprio ``$APPIMAGE`` é
//! re-executado com --helper.
QStringList helperCommand(const QString &appDir);

//! Backend de operações privilegiadas (pede a senha uma vez, no início).
//! Em falha retorna nulo e preenche ``error``.
std::unique_ptr<Backend> connectBackend(const QString &appDir, QString *error);

}  // namespace sm
