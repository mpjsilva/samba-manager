// Tema escuro fixo (paleta "darkly" da versão Python) aplicado via Fusion +
// QPalette + folha de estilo. Botões, rótulos e pílulas escolhem a aparência
// pelas propriedades dinâmicas "kind", "variant" e "role".
#pragma once

#include <QColor>
#include <QString>

class QApplication;

namespace sm::ui::theme {

inline const QColor bg{"#222222"}, side{"#161616"}, card{"#2a2a2a"}, input{"#2f2f2f"},
    border{"#3a3a3a"}, fg{"#ffffff"}, muted{"#a2a2a2"}, primary{"#375a7f"}, secondary{"#444444"},
    success{"#00bc8c"}, info{"#3498db"}, warning{"#f39c12"}, danger{"#e74c3c"};

//! Cor de destaque pelo nome ("primary", "success", ...); muted se desconhecido.
QColor color(const QString &kind);
//! Mistura a com b (t=0 -> a, t=1 -> b).
QColor blend(const QColor &a, const QColor &b, double t);
//! Versão legível de uma cor de destaque para usar como texto.
QColor ink(const QColor &c);

//! Estilo Fusion, paleta, fontes e folha de estilo.
void apply(QApplication &app);
QString monoFamily();

}  // namespace sm::ui::theme
