#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <functional>

struct ApiReply {
    int status = 0;
    QByteArray body;
    QUrl location;
    QString error;
    QByteArray etag;
    bool notModified = false;   // 由 304 + 本機快取還原（不計入 GitHub 限流）
    int rateRemaining = -1;
    qint64 rateReset = 0;       // epoch 秒
    int retryAfter = 0;         // 秒
    // 若本次回應是限流，回傳建議等待秒數；否則 0
    int rateLimitWaitSeconds() const;
    QJsonObject object() const { return QJsonDocument::fromJson(body).object(); }
};

// GitHub REST API 封裝（PAT 由 PatProvider 提供）。
class GitHubClient : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const ApiReply&)>;
    using ProgressCb = std::function<void(qint64, qint64)>;
    using DoneCb = std::function<void(bool, const QString&)>;

    explicit GitHubClient(QObject* parent = nullptr);

    void getRepo(Callback cb);
    void getFile(const QString& path, Callback cb);
    void putFile(const QString& path, const QString& message, const QByteArray& content,
                 const QString& sha, Callback cb);
    void dispatch(const QString& ref, const QJsonObject& inputs, Callback cb);
    void listRuns(Callback cb);
    void getRun(qint64 runId, Callback cb);
    void getJobs(qint64 runId, Callback cb);
    void cancelRun(qint64 runId, Callback cb);
    void listArtifacts(qint64 runId, Callback cb);
    void getJobLogs(qint64 jobId, Callback cb);
    void downloadArtifact(qint64 artifactId, const QString& destFile, ProgressCb prog, DoneCb done);
    void abortDownload();

private:
    QUrl repoUrl(const QString& suffix) const;
    void request(const QByteArray& method, const QUrl& url, const QByteArray& body, Callback cb,
                 bool github = true, bool cacheable = false);

    QNetworkAccessManager m_nam;
    QHash<QString, QPair<QByteArray, QByteArray>> m_etagCache; // url -> (etag, body)
    QPointer<QNetworkReply> m_download;
};
