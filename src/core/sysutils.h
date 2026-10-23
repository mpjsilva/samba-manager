// Utilitários de sistema: subprocessos, arquivos, serviços, usuários.
#pragma once

#include <QByteArray>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>

#include <optional>
#include <sys/types.h>

namespace sm {

struct RunResult {
    int rc = 0;
    QString out;  //!< stdout + stderr, aparados
};

//! Executa um programa (sem shell). Sem ``input``, o filho recebe /dev/null no
//! stdin — no auxiliar root o stdin é o canal da interface.
RunResult run(const QStringList &cmd, const std::optional<QString> &input = std::nullopt,
              int timeoutSec = 120);

//! Caminho do executável no PATH, ou vazio.
QString which(const QString &cmd);

//! {serviço: (ActiveState, UnitFileState)} com uma única chamada ao systemctl.
QMap<QString, QPair<QString, QString>> serviceStates(const QStringList &names);

std::optional<QByteArray> readBytes(const QString &path);

//! Gravação atômica (temporário no mesmo diretório + rename), preservando dono
//! e modo do arquivo original e restaurando o contexto SELinux. Lança em erro.
void writeFile(const QString &path, const QByteArray &data);

bool isBinary(const QString &path, const QByteArray &data);

struct Owner {
    uid_t uid;
    gid_t gid;
};

//! Quem abriu o programa como root via pkexec/sudo (PKEXEC_UID/SUDO_UID).
std::optional<Owner> invokingUser();
QString userName(uid_t uid);

//! Cria ``path`` e pais ausentes; retorna as pastas criadas (pai -> filho).
//! Só a pasta final muda de dono (``owner``) e recebe 2775; os pais criados
//! ficam root:root 0755. Lança em erro.
QStringList makeShareDir(const QString &path, const std::optional<Owner> &owner);

//! Como os.path.realpath: resolve symlinks do trecho existente e normaliza o resto.
QString realPath(const QString &path);

}  // namespace sm
