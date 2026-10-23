// SELinux: rotulagem de pastas compartilhadas (samba_share_t).
#pragma once

#include <QList>
#include <QPair>
#include <QString>

namespace sm {

//! Por que ``path`` não pode receber samba_share_t (recursivo e permanente),
//! ou vazio se puder: árvores do sistema, homes inteiras e .ssh/.gnupg/.pki.
QString labelBlockReason(const QString &path);
//! Aplica samba_share_t em ``path``. Idempotente. (ok, mensagem)
QPair<bool, QString> labelPath(const QString &path);
//! Rotula todas as pastas de compartilhamento do smb.conf.
QList<QPair<bool, QString>> relabelAllShares();

}  // namespace sm
