// Operações privilegiadas expostas pelo auxiliar root.
//
// A interface roda como usuário comum e só consegue pedir as operações
// registradas aqui; cada uma valida os próprios argumentos — não existe
// operação genérica de "rodar comando" ou "gravar arquivo". Argumentos e
// retornos são JSON. Argumento inválido lança std::invalid_argument.
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>

#include <functional>

namespace sm {

using OpFn = std::function<QJsonValue(const QJsonObject &args)>;
using OpMap = QMap<QString, OpFn>;

const OpMap &operations();

}  // namespace sm
