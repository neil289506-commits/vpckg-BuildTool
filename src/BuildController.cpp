#include "BuildController.h"
#include "Config.h"
#include "PatProvider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUuid>

namespace {
constexpr int kStepsWeightPercent = 85; // 0~85%：雲端步驟；85~100%：下載
}

BuildController::BuildController(QObject* parent) : QObject(parent)
{
    m_timer.setInterval(4000);
    connect(&m_timer, &QTimer::timeout, this, [this]() { poll(); });
}

QString BuildController::describe(const ApiReply& r, const QString& prefix) const
{
    QString detail = r.status == 0 ? r.error : r.object().value(QStringLiteral("message")).toString();
    QString hint;
    if (r.status == 401)
        hint = QStringLiteral("（PAT 無效或已過期）");
    else if (r.status == 403 || r.status == 404)
        hint = QStringLiteral("（請確認倉庫存在，且 PAT 具備 Contents / Workflows / Actions 讀寫權限）");
    return QStringLiteral("%1 [HTTP %2] %3 %4").arg(prefix).arg(r.status).arg(detail, hint);
}

void BuildController::start(const BuildRequest& req)
{
    if (m_busy)
        return;
    if (PatProvider::token().isEmpty()) {
        emit finished(false,
                      QStringLiteral("此版本沒有 PAT。請執行 tools/gen-pat.ps1，或設定環境變數 VCPKG_COMPILE_PAT。"),
                      QString());
        return;
    }
    m_busy = true;
    ++m_gen;
    m_req = req;
    m_runId = 0;
    m_jobId = 0;
    m_findAttempts = 0;
    m_tick = 0;
    m_pollBusy = false;
    m_logBusy = false;
    m_elapsed.start();
    m_timer.setInterval(4000);
    m_clientId = QUuid::createUuid().toString(QUuid::Id128).left(12);

    emit progressChanged(0);
    emit phaseChanged(QStringLiteral("檢查遠端倉庫…"));
    const int gen = m_gen;
    m_gh.getRepo([this, gen](const ApiReply& r) {
        if (gen != m_gen)
            return;
        if (r.status != 200) {
            fail(describe(r, QStringLiteral("無法存取倉庫 %1/%2").arg(Config::kRepoOwner, Config::kRepoName)));
            return;
        }
        m_branch = r.object().value(QStringLiteral("default_branch")).toString(QStringLiteral("main"));
        ensureWorkflow(gen);
    });
}

void BuildController::ensureWorkflow(int gen)
{
    emit phaseChanged(QStringLiteral("同步 workflow（yml）…"));
    QFile f(QStringLiteral(":/resources/build.yml"));
    if (!f.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("內嵌的 build.yml 讀取失敗"));
        return;
    }
    QByteArray want = f.readAll();
    want.replace("\r\n", "\n");

    m_gh.getFile(Config::kWorkflowPath, [this, gen, want](const ApiReply& r) {
        if (gen != m_gen)
            return;
        QString sha;
        if (r.status == 200) {
            const QJsonObject o = r.object();
            sha = o.value(QStringLiteral("sha")).toString();
            QString b64 = o.value(QStringLiteral("content")).toString();
            b64.remove(QLatin1Char('\n'));
            QByteArray current = QByteArray::fromBase64(b64.toLatin1());
            current.replace("\r\n", "\n");
            if (current == want) {
                dispatchRun(gen, 0);
                return;
            }
        } else if (r.status != 404) {
            fail(describe(r, QStringLiteral("讀取 workflow 失敗")));
            return;
        }
        emit phaseChanged(QStringLiteral("推送 workflow 到倉庫…"));
        m_gh.putFile(Config::kWorkflowPath, QStringLiteral("Update vcpkg compile workflow"), want, sha,
                     [this, gen](const ApiReply& pr) {
                         if (gen != m_gen)
                             return;
                         if (pr.status != 200 && pr.status != 201) {
                             fail(describe(pr, QStringLiteral("推送 workflow 失敗")));
                             return;
                         }
                         dispatchRun(gen, 0);
                     });
    });
}

void BuildController::dispatchRun(int gen, int attempt)
{
    emit phaseChanged(QStringLiteral("觸發編譯（帶入參數）…"));
    QJsonObject inputs;
    inputs.insert(QStringLiteral("package"), m_req.package);
    inputs.insert(QStringLiteral("triplet"), m_req.triplet);
    inputs.insert(QStringLiteral("features"), m_req.features);
    inputs.insert(QStringLiteral("extra_args"), m_req.extraArgs);
    inputs.insert(QStringLiteral("client_id"), m_clientId);

    m_gh.dispatch(m_branch, inputs, [this, gen, attempt](const ApiReply& r) {
        if (gen != m_gen)
            return;
        if (r.status == 200 || r.status == 204) {
            // 200：return_run_details 直接給 run id（不需要列舉 runs）；204：舊行為，之後以 client_id 比對
            const qint64 id = r.object().value(QStringLiteral("workflow_run_id")).toInteger();
            if (id > 0)
                m_runId = id;
            emit phaseChanged(m_runId ? QStringLiteral("已觸發，雲端工作已建立…")
                                      : QStringLiteral("已觸發，等待 GitHub 建立 run…"));
            m_timer.start();
            QTimer::singleShot(1500, this, [this]() { poll(); });
            return;
        }
        if (const int w = r.rateLimitWaitSeconds(); w > 0 && attempt < 6) {
            emit phaseChanged(QStringLiteral("GitHub API 限流中，約 %1 秒後重試…").arg(qMin(w, 300)));
            QTimer::singleShot(qMin(w, 300) * 1000 + 1000, this, [this, gen, attempt]() {
                if (gen == m_gen)
                    dispatchRun(gen, attempt + 1);
            });
            return;
        }
        // 剛推送 yml 後，GitHub 可能還沒索引到 workflow_dispatch 觸發器
        if ((r.status == 404 || r.status == 422) && attempt < 6) {
            QTimer::singleShot(4000, this, [this, gen, attempt]() {
                if (gen == m_gen)
                    dispatchRun(gen, attempt + 1);
            });
            return;
        }
        fail(describe(r, QStringLiteral("觸發 workflow 失敗")));
    });
}

void BuildController::poll()
{
    if (!m_busy || m_pollBusy)
        return;
    m_pollBusy = true;
    const int gen = m_gen;
    // 越久越慢：前 2 分鐘 4 秒、之後 6 秒、10 分鐘後 10 秒（長時間編譯不需要高頻）
    const qint64 sec = m_elapsed.elapsed() / 1000;
    const int want = sec < 120 ? 4000 : (sec < 600 ? 6000 : 10000);
    if (m_timer.interval() != want)
        m_timer.setInterval(want);

    if (m_runId == 0) {
        m_gh.listRuns([this, gen](const ApiReply& r) {
            if (gen != m_gen)
                return;
            if (pauseIfRateLimited(r, gen))
                return;
            m_pollBusy = false;
            const QJsonArray runs = r.object().value(QStringLiteral("workflow_runs")).toArray();
            for (const QJsonValue& v : runs) {
                const QJsonObject o = v.toObject();
                if (o.value(QStringLiteral("display_title")).toString().contains(m_clientId)) {
                    m_runId = o.value(QStringLiteral("id")).toInteger();
                    break;
                }
            }
            if (m_runId == 0 && ++m_findAttempts > 45)
                fail(QStringLiteral("找不到對應的 workflow run（逾時）"));
        });
        return;
    }

    m_gh.getRun(m_runId, [this, gen](const ApiReply& r) {
        if (gen != m_gen)
            return;
        if (pauseIfRateLimited(r, gen))
            return;
        if (r.status != 200) { // 暫時性錯誤，下一輪再試
            m_pollBusy = false;
            return;
        }
        const QJsonObject ro = r.object();
        const QString status = ro.value(QStringLiteral("status")).toString();
        const QString conclusion = ro.value(QStringLiteral("conclusion")).toString();
        m_gh.getJobs(m_runId, [this, gen, status, conclusion](const ApiReply& jr) {
            if (gen != m_gen)
                return;
            if (pauseIfRateLimited(jr, gen))
                return;
            m_pollBusy = false;
            if (jr.status == 200)
                applyJobs(jr.object());
            if (status == QLatin1String("completed")) {
                m_timer.stop();
                onRunCompleted(conclusion);
            }
        });
    });
}

void BuildController::applyJobs(const QJsonObject& jobsObj)
{
    const QJsonArray jobs = jobsObj.value(QStringLiteral("jobs")).toArray();
    if (jobs.isEmpty()) {
        emit phaseChanged(QStringLiteral("排隊中，等待 Windows Runner…"));
        return;
    }
    const QJsonObject job = jobs.first().toObject();
    m_jobId = job.value(QStringLiteral("id")).toInteger();

    QList<StepInfo> steps;
    double total = 0, done = 0;
    QString current;
    for (const QJsonValue& v : job.value(QStringLiteral("steps")).toArray()) {
        const QJsonObject s = v.toObject();
        StepInfo si{s.value(QStringLiteral("name")).toString(), s.value(QStringLiteral("status")).toString(),
                    s.value(QStringLiteral("conclusion")).toString()};
        // 編譯步驟耗時遠大於其他步驟，給較高權重讓進度條較貼近實際
        const double w = si.name.contains(QStringLiteral("編譯")) ? 6.0 : 1.0;
        total += w;
        if (si.status == QLatin1String("completed"))
            done += w;
        else if (si.status == QLatin1String("in_progress")) {
            done += w * 0.5;
            current = si.name;
        }
        steps << si;
    }
    emit stepsChanged(steps);
    if (total > 0)
        emit progressChanged(int(done / total * kStepsWeightPercent));
    if (!current.isEmpty())
        emit phaseChanged(QStringLiteral("雲端執行中：") + current);

    if (job.value(QStringLiteral("status")).toString() == QLatin1String("in_progress") && (m_tick++ % 8 == 0))
        fetchLogs(m_gen);
}

void BuildController::fetchLogs(int gen)
{
    if (m_jobId == 0 || m_logBusy)
        return;
    m_logBusy = true;
    // 盡力而為：執行中的 job log 不一定能取得，失敗就略過
    m_gh.getJobLogs(m_jobId, [this, gen](const ApiReply& r) {
        m_logBusy = false;
        if (gen != m_gen || r.status != 200)
            return;
        static const QRegularExpression ansi(QStringLiteral("\\x1b\\[[0-9;]*[A-Za-z]"));
        static const QRegularExpression ts(QStringLiteral("^\\d{4}-\\d\\d-\\d\\dT[\\d:.]+Z "));
        QStringList lines = QString::fromUtf8(r.body).split(QLatin1Char('\n'));
        if (lines.size() > 80)
            lines = lines.mid(lines.size() - 80);
        for (QString& l : lines) {
            l.remove(ansi);
            l.remove(ts);
            l = l.trimmed();
        }
        emit logTailChanged(lines.join(QLatin1Char('\n')));
    });
}

void BuildController::onRunCompleted(const QString& conclusion)
{
    const int gen = m_gen;
    fetchLogs(gen);
    emit progressChanged(kStepsWeightPercent);
    emit phaseChanged(QStringLiteral("雲端工作結束（%1），取得產物…").arg(conclusion));
    fetchArtifact(gen, conclusion == QLatin1String("success"), 0);
}

QString BuildController::uniquePath(const QString& baseName) const
{
    const QDir dir(m_req.saveDir);
    QString path = dir.filePath(baseName + QStringLiteral(".zip"));
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = dir.filePath(QStringLiteral("%1 (%2).zip").arg(baseName).arg(n));
    return path;
}

void BuildController::fetchArtifact(int gen, bool success, int attempt)
{
    m_gh.listArtifacts(m_runId, [this, gen, success, attempt](const ApiReply& r) {
        if (gen != m_gen)
            return;
        const QString wanted =
            (success ? QStringLiteral("vcpkg-") : QStringLiteral("logs-")) + m_clientId;
        qint64 id = 0;
        for (const QJsonValue& v : r.object().value(QStringLiteral("artifacts")).toArray()) {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("name")).toString() == wanted) {
                id = o.value(QStringLiteral("id")).toInteger();
                break;
            }
        }
        if (id == 0) {
            if (attempt < 3) {
                QTimer::singleShot(2500, this, [this, gen, success, attempt]() {
                    if (gen == m_gen)
                        fetchArtifact(gen, success, attempt + 1);
                });
                return;
            }
            finish(false,
                   success ? QStringLiteral("找不到編譯產物")
                           : QStringLiteral("編譯失敗，且沒有可下載的 log（請到 GitHub Actions 頁面查看）"),
                   QString());
            return;
        }

        QDir().mkpath(m_req.saveDir);
        const QString base = QStringLiteral("%1-%2%3").arg(m_req.package, m_req.triplet,
                                                           success ? QString() : QStringLiteral("-logs"));
        const QString dest = uniquePath(base);
        emit phaseChanged(QStringLiteral("下載中…"));
        m_gh.downloadArtifact(
            id, dest,
            [this, gen](qint64 got, qint64 total) {
                if (gen != m_gen || total <= 0)
                    return;
                emit progressChanged(kStepsWeightPercent + int(double(got) / double(total) * (100 - kStepsWeightPercent)));
                emit phaseChanged(QStringLiteral("下載中… %1 / %2 MB")
                                      .arg(got / 1048576.0, 0, 'f', 1)
                                      .arg(total / 1048576.0, 0, 'f', 1));
            },
            [this, gen, success, dest](bool ok, const QString& err) {
                if (gen != m_gen)
                    return;
                if (!ok) {
                    finish(false, QStringLiteral("下載失敗：") + err, QString());
                    return;
                }
                emit progressChanged(100);
                if (success)
                    finish(true, QStringLiteral("完成！已下載到：\n") + dest, dest);
                else
                    finish(false, QStringLiteral("編譯失敗，log 已下載到：\n") + dest, dest);
            });
    });
}

bool BuildController::pauseIfRateLimited(const ApiReply& r, int gen)
{
    const int w = r.rateLimitWaitSeconds();
    if (w <= 0)
        return false;
    m_pollBusy = false;
    m_timer.stop();
    const int wait = qMin(w, 900);
    emit phaseChanged(QStringLiteral("GitHub API 限流中，約 %1 秒後自動繼續…").arg(wait));
    QTimer::singleShot(wait * 1000 + 1000, this, [this, gen]() {
        if (gen == m_gen && m_busy)
            m_timer.start();
    });
    return true;
}

void BuildController::cancel()
{
    if (!m_busy)
        return;
    m_timer.stop();
    ++m_gen; // 讓所有進行中的回呼失效
    m_gh.abortDownload();
    if (m_runId != 0)
        m_gh.cancelRun(m_runId, [](const ApiReply&) {});
    m_busy = false;
    emit finished(false, QStringLiteral("已取消"), QString());
}

void BuildController::fail(const QString& message)
{
    ++m_gen;
    finish(false, message, QString());
}

void BuildController::finish(bool ok, const QString& message, const QString& file)
{
    m_timer.stop();
    m_busy = false;
    emit finished(ok, message, file);
}
