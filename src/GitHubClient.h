#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <functional>

struct ApiReply {
    int status = 0;
    QByteArray body;
    QUrl location;
    QString error;
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
                 bool github = true);

    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_download;
};
