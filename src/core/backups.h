// Backup versionado + log de alterações dos arquivos monitorados.
#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

namespace sm {

struct Version {
    QString name;  //!< AAAAMMDD-HHMMSS-uuuuuu.bak
    QString reason;
    qint64 size = 0;
};

QString fileDir(const QString &path, bool create);
//! Salva uma versão (lê o arquivo se ``data`` vier vazio) e mantém só as
//! últimas kMaxVersions. Retorna o nome, ou vazio se não houver o que salvar.
QString backupFile(const QString &path, const QString &reason,
                   const std::optional<QByteArray> &data = std::nullopt);
//! Da mais nova para a mais antiga. Não cria diretórios.
QList<Version> listVersions(const QString &path);
bool isValidVersionName(const QString &name);
std::optional<QByteArray> readVersion(const QString &path, const QString &name);
QString versionLabel(const QString &name);
void logChange(const QString &path, const QString &action, const QString &detail = {});
QString readLog(const QString &path);
QString describeChange(const QString &path, const std::optional<QByteArray> &before,
                       const std::optional<QByteArray> &after);

struct PassdbEntry {
    QString flags, ntHash, lct;
    bool operator==(const PassdbEntry &o) const = default;
};
using PassdbEntries = QMap<QString, PassdbEntry>;

//! Saída de ``pdbedit -L -w`` -> {usuário: entrada}.
PassdbEntries parseSmbpasswd(const QString &text);
//! Resumo legível, sem expor hashes.
QString describePassdbChange(const PassdbEntries &before, const PassdbEntries &after);

//! Estado de um arquivo monitorado. O passdb.tdb é copiado via ``pdbedit -e``
//! (respeita os locks do smbd) e comparado pelas contas, já que o export não é
//! byte a byte determinístico.
struct FileState {
    std::optional<QByteArray> data;  //!< bytes para o backup
    std::optional<PassdbEntries> entries;  //!< só para o passdb
    bool sameAs(const FileState &o) const;
};
FileState captureState(const QString &path);

//! Registra mudanças nos arquivos monitorados durante um escopo: ao sair, para
//! cada arquivo alterado guarda o estado ANTERIOR como backup e escreve no log.
//! Saída por exceção marca a ação como "(interrompido por erro)".
//!
//!     { Tracker t("Adicionar usuário joao"); ... }
class Tracker {
public:
    explicit Tracker(QString action, QStringList paths = {}, QStringList notes = {});
    ~Tracker();
    Tracker(const Tracker &) = delete;
    Tracker &operator=(const Tracker &) = delete;

private:
    QString m_action;
    QStringList m_paths, m_notes;
    QList<FileState> m_before;
    int m_uncaught;
};

}  // namespace sm
