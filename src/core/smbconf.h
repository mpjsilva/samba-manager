// Parser do smb.conf que preserva comentários, ordem e formatação.
#pragma once

#include "core/sysutils.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace sm {

//! Parâmetros de uma seção, na ordem em que aparecem (chaves em minúsculas).
class Params {
public:
    void set(const QString &key, const QString &value);
    QString value(const QString &key, const QString &fallback = {}) const;
    bool contains(const QString &key) const;
    void remove(const QString &key);
    const QList<QPair<QString, QString>> &items() const { return m_items; }
    bool operator==(const Params &o) const { return m_items == o.m_items; }

private:
    QList<QPair<QString, QString>> m_items;
};

class SmbConf {
public:
    explicit SmbConf(const QString &text = {});
    //! Lê o smb.conf do sistema (ou um [global] mínimo se não existir).
    static SmbConf load();

    QStringList names() const;
    bool has(const QString &name) const;
    Params get(const QString &name) const;
    //! Seções que não estão em ``skip`` (minúsculas), na ordem do arquivo.
    QList<QPair<QString, Params>> shares(const QStringList &skip = {QStringLiteral("global")}) const;

    //! Define o conjunto COMPLETO de parâmetros da seção, criando-a se preciso.
    //! Valores vazios e chaves ausentes são removidos. Numa seção existente só
    //! as linhas que mudam são tocadas; diretivas repetíveis (include) nunca são
    //! fundidas. Lança std::invalid_argument com caracteres de controle.
    void set(const QString &name, const Params &params);
    void remove(const QString &name);
    QString dump() const;

private:
    struct Section {
        QString name;  // vazio = preâmbulo
        QStringList lines;  // inclui o cabeçalho [nome]
    };
    const Section *find(const QString &name) const;
    Section *find(const QString &name);
    QList<Section> m_sections;
};

//! Seções com significado especial no Samba: nunca viram compartilhamento comum.
bool isReservedSection(const QString &name);
bool hasControlChars(const QString &text);
//! Motivo pelo qual ``name`` não pode ser usado, ou vazio se estiver ok.
QString shareNameError(const QString &name, const QStringList &existing, bool creating);

//! Valida um smb.conf com testparm.
RunResult testConfBytes(const QByteArray &data);
//! Valida, faz backup/log (Tracker), grava e recarrega. (ok, mensagem)
QPair<bool, QString> saveConf(const SmbConf &conf, const QString &action,
                              const QStringList &notes = {});

}  // namespace sm
