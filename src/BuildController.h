#pragma once
#include "GitHubClient.h"

#include <QList>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

struct BuildRequest {
    QString package;
    QString triplet;
    QString features;
    QString extraArgs;
    QString saveDir;
};

struct StepInfo {
    QString name;
    QString status;     // queued / in_progress / completed
    QString conclusion; // success / failure / skipped ...
};

// 流程：確認倉庫 → 同步 workflow(yml) → dispatch(帶入編譯參數) → 追蹤 run/job/steps → 下載 artifact
class BuildController : public QObject {
    Q_OBJECT
public:
    explicit BuildController(QObject* parent = nullptr);

    bool busy() const { return m_busy; }
    void start(const BuildRequest& req);
    void cancel();

signals:
    void phaseChanged(const QString& text);
    void progressChanged(int percent);
    void stepsChanged(const QList<StepInfo>& steps);
    void logTailChanged(const QString& text);
    void finished(bool ok, const QString& message, const QString& file);

private:
    void ensureWorkflow(int gen);
    void dispatchRun(int gen, int attempt);
    void poll();
    void applyJobs(const QJsonObject& jobsObj);
    void fetchLogs(int gen);
    void onRunCompleted(const QString& conclusion);
    void fetchArtifact(int gen, bool success, int attempt);
    bool pauseIfRateLimited(const ApiReply& r, int gen);
    void fail(const QString& message);
    void finish(bool ok, const QString& message, const QString& file);
    QString describe(const ApiReply& r, const QString& prefix) const;
    QString uniquePath(const QString& baseName) const;

    GitHubClient m_gh;
    QTimer m_timer;
    QElapsedTimer m_elapsed;
    BuildRequest m_req;
    QString m_clientId;
    QString m_branch;
    qint64 m_runId = 0;
    qint64 m_jobId = 0;
    int m_gen = 0;
    int m_findAttempts = 0;
    int m_tick = 0;
    bool m_busy = false;
    bool m_pollBusy = false;
    bool m_logBusy = false;
};
