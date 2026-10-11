#include "VcpkgManager.h"
#include "Config.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

VcpkgManager::VcpkgManager(QObject* parent) : QObject(parent) {}

QString VcpkgManager::root() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/vcpkg");
}

QString VcpkgManager::exePath() const { return root() + QStringLiteral("/vcpkg.exe"); }

bool VcpkgManager::isReady() const { return QFileInfo::exists(exePath()); }

void VcpkgManager::emitLines(const QString& text)
{
    QString t = text;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = t.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& l : lines)
        emit log(l);
}

void VcpkgManager::startProc(const QString& program, const QStringList& args, const QString& workDir,
                             std::function<void(bool)> onDone)
{
    auto* p = new QProcess(this);
    m_proc = p;
    p->setProcessChannelMode(QProcess::MergedChannels);
    if (!workDir.isEmpty())
        p->setWorkingDirectory(workDir);

    connect(p, &QProcess::readyRead, this, [this, p]() { emitLines(QString::fromLocal8Bit(p->readAll())); });
    connect(p, &QProcess::errorOccurred, this, [this, p, onDone](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_proc = nullptr;
            p->deleteLater();
            onDone(false);
        }
    });
    connect(p, &QProcess::finished, this, [this, p, onDone](int code, QProcess::ExitStatus st) {
        emitLines(QString::fromLocal8Bit(p->readAll()));
        m_proc = nullptr;
        p->deleteLater();
        onDone(st == QProcess::NormalExit && code == 0);
    });

    emit log(program + QLatin1Char(' ') + args.join(QLatin1Char(' ')));
    p->start(program, args);
}

void VcpkgManager::ensureReady()
{
    if (isReady()) {
        emit readyChanged(true, QStringLiteral("vcpkg 已就緒"));
        return;
    }
    if (m_proc)
        return;

    if (QFileInfo::exists(root() + QStringLiteral("/bootstrap-vcpkg.bat"))) {
        runBootstrap();
        return;
    }

    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty()) {
        emit readyChanged(false, QStringLiteral("找不到 git，請先安裝 Git for Windows 並重新檢查"));
        return;
    }

    QDir rootDir(root());
    if (rootDir.exists())
        rootDir.removeRecursively(); // 殘留的不完整 clone
    QDir().mkpath(QFileInfo(root()).absolutePath());

    emit readyChanged(false, QStringLiteral("首次啟動：正在 clone vcpkg…"));
    startProc(git, {QStringLiteral("clone"), QStringLiteral("--depth"), QStringLiteral("1"),
                    Config::kVcpkgGitUrl, root()},
              QString(), [this](bool ok) {
                  if (ok)
                      runBootstrap();
                  else
                      emit readyChanged(false, QStringLiteral("git clone vcpkg 失敗（請查看本機 Log）"));
              });
}

void VcpkgManager::runBootstrap()
{
    emit readyChanged(false, QStringLiteral("正在取得 vcpkg.exe（bootstrap）…"));
    startProc(QStringLiteral("cmd.exe"),
              {QStringLiteral("/c"), QStringLiteral("bootstrap-vcpkg.bat"), QStringLiteral("-disableMetrics")},
              root(), [this](bool ok) {
                  if (ok && isReady())
                      emit readyChanged(true, QStringLiteral("vcpkg 已就緒"));
                  else
                      emit readyChanged(false, QStringLiteral("bootstrap 失敗（請查看本機 Log）"));
              });
}

void VcpkgManager::search(const QString& query)
{
    if (!isReady() || query.trimmed().isEmpty())
        return;

    if (QProcess* old = m_search) {
        m_search = nullptr; // 舊的 finished 會因 p != m_search 被忽略
        old->kill();
    }

    auto* p = new QProcess(this);
    m_search = p;
    p->setWorkingDirectory(root());
    connect(p, &QProcess::finished, this, [this, p, query](int code, QProcess::ExitStatus st) {
        if (p != m_search) {
            p->deleteLater();
            return;
        }
        m_search = nullptr;
        const QString out = QString::fromLocal8Bit(p->readAllStandardOutput());
        p->deleteLater();

        QStringList names;
        if (st == QProcess::NormalExit && code == 0) {
            // 輸出格式：「名稱  版本  說明」。名稱一律小寫；提示句以大寫開頭，不會被誤判。
            static const QRegularExpression re(QStringLiteral("^([a-z0-9][a-z0-9\\-]*)\\s+\\S"));
            QSet<QString> seen;
            for (const QString& line : out.split(QLatin1Char('\n'))) {
                const auto m = re.match(line);
                if (m.hasMatch() && !seen.contains(m.captured(1))) {
                    seen.insert(m.captured(1));
                    names << m.captured(1);
                    if (names.size() >= 60)
                        break;
                }
            }
        }
        emit searchFinished(query, names);
    });
    p->start(exePath(), {QStringLiteral("search"), query, QStringLiteral("--disable-metrics")});
}
