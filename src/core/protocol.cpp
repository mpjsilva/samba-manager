#include "core/protocol.h"

#include "core/sysutils.h"

#include <QFileInfo>
#include <QJsonDocument>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <optional>
#include <vector>

extern char **environ;

namespace sm {

namespace {

const QByteArray kReady = QByteArrayLiteral(R"({"ready":true})");

bool writeAll(int fd, const QByteArray &data)
{
    qsizetype done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.constData() + done, static_cast<size_t>(data.size() - done));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        done += n;
    }
    return true;
}

//! Lê uma linha (sem o \n) de ``fd`` usando ``buf`` como sobra. Vazio no fim.
std::optional<QByteArray> readLineFd(int fd, QByteArray &buf)
{
    for (;;) {
        const qsizetype nl = buf.indexOf('\n');
        if (nl >= 0) {
            QByteArray line = buf.left(nl);
            buf.remove(0, nl + 1);
            return line;
        }
        char chunk[4096];
        const ssize_t n = ::read(fd, chunk, sizeof chunk);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            if (buf.isEmpty())
                return std::nullopt;
            QByteArray line = buf;
            buf.clear();
            return line;
        }
        buf.append(chunk, n);
    }
}

QByteArray compact(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

}  // namespace

QString startFailureMessage(int rc, const QStringList &stderrLines)
{
    QString head;
    if (rc == 126)
        head = QStringLiteral("Autenticação cancelada: a janela de senha foi fechada.");
    else if (rc == 127)
        head = QStringLiteral("Autorização negada: senha incorreta, usuário sem permissão de "
                              "administrador ou nenhum agente de autenticação (polkit) "
                              "disponível para pedir a senha.");
    else
        head = QStringLiteral("O processo administrador não iniciou (código %1).").arg(rc);
    QString msg = head + QStringLiteral("\n\nO Samba Manager precisa de privilégios de administrador.");
    QStringList kept;
    for (const QString &ln : stderrLines)
        if (!ln.trimmed().isEmpty())
            kept << ln;
    const QString detail = kept.join('\n').right(600);
    if (!detail.isEmpty())
        msg += QStringLiteral("\n\nDetalhes:\n") + detail;
    return msg;
}

// ---- lado root ----

void serveLoop(int inFd, int outFd, const OpMap &ops)
{
    if (!writeAll(outFd, kReady + '\n'))
        return;
    QByteArray buf;
    while (const auto line = readLineFd(inFd, buf)) {
        if (line->trimmed().isEmpty())
            continue;
        QJsonObject resp;
        try {
            QJsonParseError perr{};
            const QJsonDocument doc = QJsonDocument::fromJson(*line, &perr);
            const QJsonObject req = doc.object();
            const QString op = req.value(QLatin1String("op")).toString();
            const auto it = ops.constFind(op);
            if (!doc.isObject() || it == ops.constEnd())
                throw std::invalid_argument(
                    (QStringLiteral("Operação desconhecida: ") + QString::fromUtf8(line->left(160)))
                        .toStdString());
            const QJsonValue args = req.value(QLatin1String("args"));
            if (!args.isUndefined() && !args.isNull() && !args.isObject())
                throw std::invalid_argument("Argumentos inválidos.");
            resp = {{"ok", true}, {"result", it.value()(args.toObject())}};
        } catch (const std::exception &e) {
            // Erro vira resposta; o auxiliar segue vivo.
            resp = {{"ok", false}, {"error", QString::fromUtf8(e.what())}};
        } catch (...) {
            resp = {{"ok", false}, {"error", QStringLiteral("erro desconhecido")}};
        }
        if (!writeAll(outFd, compact(resp) + '\n'))
            return;
    }
}

int serveMain()
{
    if (::geteuid() != 0) {
        std::fputs("O auxiliar precisa rodar como root (via pkexec).\n", stderr);
        return 1;
    }
    ::umask(022);
    ::signal(SIGPIPE, SIG_IGN);
    // O protocolo usa cópias de stdin/stdout; os originais passam a apontar
    // para /dev/null e stderr, então nem uma saída perdida nem um subprocesso
    // que herde os descritores consegue corromper a conversa.
    const int in = ::fcntl(0, F_DUPFD_CLOEXEC, 3);
    const int out = ::fcntl(1, F_DUPFD_CLOEXEC, 3);
    const int devnull = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (in < 0 || out < 0 || devnull < 0 || ::dup2(devnull, 0) < 0 || ::dup2(2, 1) < 0) {
        std::perror("samba-manager-ng-helper");
        return 1;
    }
    ::close(devnull);
    serveLoop(in, out, operations());
    return 0;
}

// ---- lado UI ----

HelperClient::HelperClient(const QStringList &cmd)
{
    int inPipe[2], outPipe[2], errPipe[2];
    if (::pipe2(inPipe, O_CLOEXEC) < 0)
        throw BackendError(QStringLiteral("pipe falhou"));
    if (::pipe2(outPipe, O_CLOEXEC) < 0 || ::pipe2(errPipe, O_CLOEXEC) < 0)
        throw BackendError(QStringLiteral("pipe falhou"));

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, inPipe[0], 0);
    posix_spawn_file_actions_adddup2(&fa, outPipe[1], 1);
    posix_spawn_file_actions_adddup2(&fa, errPipe[1], 2);

    QList<QByteArray> store;
    std::vector<char *> argv;
    for (const QString &a : cmd)
        store << a.toLocal8Bit();
    for (QByteArray &a : store)
        argv.push_back(a.data());
    argv.push_back(nullptr);

    const int err = ::posix_spawnp(&m_pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    ::close(inPipe[0]);
    ::close(outPipe[1]);
    ::close(errPipe[1]);
    m_in = inPipe[1];
    m_out = outPipe[0];
    if (err != 0) {
        m_pid = -1;
        ::close(errPipe[0]);
        throw BackendError(startFailureMessage(
            127, {QStringLiteral("não foi possível executar %1: %2")
                      .arg(cmd.value(0), QString::fromLocal8Bit(std::strerror(err)))}));
    }
    m_stderrThread = std::thread(&HelperClient::drainStderr, this, errPipe[0]);

    const QByteArray first = readLine();
    if (first.trimmed() != kReady) {
        const int rc = finish();
        QStringList tail;
        {
            std::lock_guard g(m_tailLock);
            tail = QStringList(m_tail.begin(), m_tail.end());
        }
        throw BackendError(startFailureMessage(rc, tail));
    }
}

HelperClient::~HelperClient() { finish(); }

void HelperClient::drainStderr(int fd)
{
    // stderr do pkexec/auxiliar: repassado ao terminal e guardado para
    // explicar uma falha de início (aberto pelo menu, ninguém veria o terminal).
    QByteArray buf;
    while (const auto line = readLineFd(fd, buf)) {
        std::fprintf(stderr, "%s\n", line->constData());
        std::lock_guard g(m_tailLock);
        m_tail.push_back(QString::fromUtf8(*line));
        if (m_tail.size() > 20)
            m_tail.pop_front();
    }
    ::close(fd);
}

QByteArray HelperClient::readLine()
{
    return readLineFd(m_out, m_buf).value_or(QByteArray());
}

int HelperClient::finish()
{
    int rc = -1;
    if (m_in >= 0) {
        ::close(m_in);  // fim do stdin: o auxiliar encerra sozinho
        m_in = -1;
    }
    if (m_pid > 0) {
        int status = 0;
        pid_t r = 0;
        for (int i = 0; i < 50 && (r = ::waitpid(m_pid, &status, WNOHANG)) == 0; ++i)
            ::usleep(100000);
        if (r == 0) {
            ::kill(m_pid, SIGKILL);
            r = ::waitpid(m_pid, &status, 0);
        }
        if (r == m_pid)
            rc = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        m_pid = -1;
    }
    if (m_out >= 0) {
        ::close(m_out);
        m_out = -1;
    }
    if (m_stderrThread.joinable())
        m_stderrThread.join();
    return rc;
}

QJsonValue HelperClient::call(const QString &op, const QJsonObject &args)
{
    QByteArray line;
    {
        std::lock_guard g(m_lock);
        if (m_in < 0 || !writeAll(m_in, compact({{"op", op}, {"args", args}}) + '\n'))
            throw BackendError(QStringLiteral("Conexão com o processo administrador perdida."));
        line = readLine();
    }
    if (line.isEmpty())
        throw BackendError(QStringLiteral("O processo administrador foi encerrado. Reabra o programa."));
    const QJsonObject resp = QJsonDocument::fromJson(line).object();
    if (resp.value(QLatin1String("ok")).toBool())
        return resp.value(QLatin1String("result"));
    const QString err = resp.value(QLatin1String("error")).toString();
    throw BackendError(err.isEmpty() ? QStringLiteral("erro desconhecido") : err);
}

QJsonValue LocalBackend::call(const QString &op, const QJsonObject &args)
{
    const auto it = m_ops.constFind(op);
    if (it == m_ops.constEnd())
        throw BackendError(QStringLiteral("Operação desconhecida: ") + op);
    try {
        return it.value()(args);
    } catch (const std::exception &e) {
        throw BackendError(QString::fromUtf8(e.what()));
    }
}

QStringList helperCommand(const QString &appDir)
{
    const QString ai = qEnvironmentVariable("APPIMAGE");
    if (!ai.isEmpty() && QFileInfo(ai).isFile())
        return {QStringLiteral("pkexec"), QFileInfo(ai).absoluteFilePath(), QStringLiteral("--helper")};
    for (const QString &rel : {QStringLiteral("samba-manager-ng-helper"),
                               QStringLiteral("../libexec/samba-manager-ng/samba-manager-ng-helper")}) {
        const QFileInfo fi(appDir + '/' + rel);
        if (fi.isFile() && fi.isExecutable())
            return {QStringLiteral("pkexec"), fi.canonicalFilePath()};
    }
    return {};
}

std::unique_ptr<Backend> connectBackend(const QString &appDir, QString *error)
{
    if (::geteuid() == 0)
        return std::make_unique<LocalBackend>();
    if (which(QStringLiteral("pkexec")).isEmpty()) {
        *error = QStringLiteral("pkexec não encontrado.\nInstale o pacote 'polkit' ou rode com sudo.");
        return nullptr;
    }
    const QStringList cmd = helperCommand(appDir);
    if (cmd.isEmpty()) {
        *error = QStringLiteral("Auxiliar samba-manager-ng-helper não encontrado ao lado do programa.");
        return nullptr;
    }
    try {
        return std::make_unique<HelperClient>(cmd);
    } catch (const BackendError &e) {
        *error = e.message();
        return nullptr;
    }
}

}  // namespace sm
