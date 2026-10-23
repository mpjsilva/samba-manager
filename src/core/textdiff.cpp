#include "core/textdiff.h"

#include <algorithm>
#include <vector>

namespace sm {

namespace {

enum class Op { Equal, Del, Add };

struct Edit {
    Op op;
    int a;  // índice em a (Equal/Del)
    int b;  // índice em b (Equal/Add)
};

std::vector<Edit> editScript(const QStringList &a, const QStringList &b)
{
    const auto n = static_cast<int>(a.size());
    const auto m = static_cast<int>(b.size());
    std::vector<Edit> out;
    constexpr long long kMaxCells = 25'000'000;
    if (static_cast<long long>(n + 1) * (m + 1) > kMaxCells) {
        for (int i = 0; i < n; ++i)
            out.push_back({Op::Del, i, 0});
        for (int j = 0; j < m; ++j)
            out.push_back({Op::Add, n, j});
        return out;
    }
    // lcs[i][j] = tamanho da LCS de a[i:] e b[j:]
    std::vector<int> lcs(static_cast<size_t>(n + 1) * (m + 1), 0);
    auto at = [&](int i, int j) -> int & { return lcs[static_cast<size_t>(i) * (m + 1) + j]; };
    for (int i = n - 1; i >= 0; --i)
        for (int j = m - 1; j >= 0; --j)
            at(i, j) = a[i] == b[j] ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
    int i = 0, j = 0;
    while (i < n && j < m) {
        if (a[i] == b[j])
            out.push_back({Op::Equal, i++, j++});
        else if (at(i + 1, j) >= at(i, j + 1))
            out.push_back({Op::Del, i++, j});
        else
            out.push_back({Op::Add, i, j++});
    }
    while (i < n)
        out.push_back({Op::Del, i++, j});
    while (j < m)
        out.push_back({Op::Add, i, j++});
    return out;
}

QString range(int start, int len)
{
    // Mesmo formato do difflib: "início,tamanho" (1-based; tamanho 1 omitido).
    if (len == 1)
        return QString::number(start + 1);
    return QStringLiteral("%1,%2").arg(len == 0 ? start : start + 1).arg(len);
}

}  // namespace

QStringList unifiedDiff(const QStringList &a, const QStringList &b, const QString &fromName,
                        const QString &toName, int context)
{
    const std::vector<Edit> ed = editScript(a, b);
    QStringList out;
    const auto total = static_cast<int>(ed.size());
    int k = 0;
    while (k < total) {
        while (k < total && ed[k].op == Op::Equal)
            ++k;
        if (k == total)
            break;
        // Hunk: começa `context` iguais antes; termina quando há > 2*context iguais seguidos.
        const int start = std::max(0, k - context);
        int end = k;
        while (end < total) {
            if (ed[end].op != Op::Equal) {
                ++end;
                continue;
            }
            int run = end;
            while (run < total && ed[run].op == Op::Equal)
                ++run;
            if (run == total || run - end > 2 * context) {
                end = std::min(run, end + context);
                break;
            }
            end = run;
        }
        if (out.isEmpty())
            out << QStringLiteral("--- ") + fromName << QStringLiteral("+++ ") + toName;
        int aStart = ed[start].a, bStart = ed[start].b, aLen = 0, bLen = 0;
        QStringList body;
        for (int x = start; x < end; ++x) {
            const Edit &e = ed[x];
            if (e.op == Op::Equal) {
                body << QStringLiteral(" ") + a[e.a];
                ++aLen, ++bLen;
            } else if (e.op == Op::Del) {
                body << QStringLiteral("-") + a[e.a];
                ++aLen;
            } else {
                body << QStringLiteral("+") + b[e.b];
                ++bLen;
            }
        }
        out << QStringLiteral("@@ -%1 +%2 @@").arg(range(aStart, aLen), range(bStart, bLen));
        out << body;
        k = end;
    }
    return out;
}

}  // namespace sm
