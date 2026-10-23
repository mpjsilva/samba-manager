// Auxiliar root: aberto pela interface via pkexec, atende operações da lista
// fechada em core/ops.cpp por JSON em stdin/stdout. Só depende do QtCore.
#include "core/protocol.h"

#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    return sm::serveMain();
}
