// Diff de linhas no formato unificado (equivalente a difflib.unified_diff).
#pragma once

#include <QString>
#include <QStringList>

namespace sm {

//! Linhas do diff unificado (cabeçalhos ---/+++ e hunks @@), vazio se iguais.
//! Arquivos grandes demais para a LCS caem num diff "tudo removido/tudo
//! adicionado" (smb.conf, passwd e group ficam muito abaixo do limite).
QStringList unifiedDiff(const QStringList &a, const QStringList &b, const QString &fromName,
                        const QString &toName, int context = 3);

}  // namespace sm
