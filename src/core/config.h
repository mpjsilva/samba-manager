// Constantes e caminhos centrais.
//
// Os caminhos podem ser trocados por variáveis de ambiente (SM_SMB_CONF,
// SM_PASSDB, SM_APP_DIR) — usado pelos testes. São lidos a cada chamada, então
// não há ordem de inicialização a respeitar. O auxiliar root é aberto pelo
// pkexec, que limpa o ambiente: o usuário não consegue redirecioná-lo.
#pragma once

#include <QList>
#include <QPair>
#include <QSet>
#include <QString>

namespace sm::config {

inline constexpr const char *kAppVersion = SM_VERSION;
inline constexpr int kMaxVersions = 10;
inline constexpr int kMaxDiffLines = 200;

QString smbConf();
QString passdb();
QString appDir();
QString backupRoot();

//! Arquivos afetados pelas operações (monitorados pelo Tracker), em ordem.
QList<QPair<QString, QString>> tracked();
bool isTracked(const QString &path);

//! Só visualização: restaurar sem /etc/shadow e /etc/gshadow deixaria as
//! contas do sistema inconsistentes.
bool isViewOnly(const QString &path);

//! Pastas que nunca recebem rótulo samba_share_t.
const QSet<QString> &systemPaths();

}  // namespace sm::config
