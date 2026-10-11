#include "GitHubClient.h"
#include "Config.h"
#include "PatProvider.h"

#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <memory>

GitHubClient::GitHubClient(QObject* parent) : QObject(parent)
{
    // 手動處理重新導向：下載連結（blob storage）不能帶 Authorization。
    m_nam.setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
}

QUrl GitHubClient::repoUrl(const QString& suffix) const
{
    return QUrl(QStringLiteral("https://api.github.com/repos/%1/%2%3")
                    .arg(Config::kRepoOwner, Config::kRepoName, suffix));
}

void GitHubClient::request(const QByteArray& method, const QUrl& url, const QByteArray& body,
                           Callback cb, bool github)
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "VcpkgCompileTool");
    if (github) {
        req.setRawHeader("Accept", "application/vnd.github+json");
        req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
        req.setRawHeader("Authorization", "Bearer " + PatProvider::token().toUtf8());
    }
    req.setTransferTimeout(30000);
    if (!body.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply* rep = (method == "GET") ? m_nam.get(req) : m_nam.sendCustomRequest(req, method, body);
    connect(rep, &QNetworkReply::finished, this, [rep, cb]() {
        ApiReply r;
        r.status = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.body = rep->readAll();
        r.location = rep->header(QNetworkRequest::LocationHeader).toUrl();
        if (r.status == 0)
            r.error = rep->errorString();
        rep->deleteLater();
        cb(r);
    });
}

void GitHubClient::getRepo(Callback cb) { request("GET", repoUrl(QString()), {}, std::move(cb)); }

void GitHubClient::getFile(const QString& path, Callback cb)
{
    request("GET", repoUrl(QStringLiteral("/contents/") + path), {}, std::move(cb));
}

void GitHubClient::putFile(const QString& path, const QString& message, const QByteArray& content,
                           const QString& sha, Callback cb)
{
    QJsonObject o;
    o.insert(QStringLiteral("message"), message);
    o.insert(QStringLiteral("content"), QString::fromLatin1(content.toBase64()));
    if (!sha.isEmpty())
        o.insert(QStringLiteral("sha"), sha);
    request("PUT", repoUrl(QStringLiteral("/contents/") + path), QJsonDocument(o).toJson(QJsonDocument::Compact),
            std::move(cb));
}

void GitHubClient::dispatch(const QString& ref, const QJsonObject& inputs, Callback cb)
{
    QJsonObject o;
    o.insert(QStringLiteral("ref"), ref);
    o.insert(QStringLiteral("inputs"), inputs);
    request("POST", repoUrl(QStringLiteral("/actions/workflows/%1/dispatches").arg(Config::kWorkflowFile)),
            QJsonDocument(o).toJson(QJsonDocument::Compact), std::move(cb));
}

void GitHubClient::listRuns(Callback cb)
{
    request("GET",
            repoUrl(QStringLiteral("/actions/workflows/%1/runs?event=workflow_dispatch&per_page=20")
                        .arg(Config::kWorkflowFile)),
            {}, std::move(cb));
}

void GitHubClient::getRun(qint64 runId, Callback cb)
{
    request("GET", repoUrl(QStringLiteral("/actions/runs/%1").arg(runId)), {}, std::move(cb));
}

void GitHubClient::getJobs(qint64 runId, Callback cb)
{
    request("GET", repoUrl(QStringLiteral("/actions/runs/%1/jobs").arg(runId)), {}, std::move(cb));
}

void GitHubClient::cancelRun(qint64 runId, Callback cb)
{
    request("POST", repoUrl(QStringLiteral("/actions/runs/%1/cancel").arg(runId)), {}, std::move(cb));
}

void GitHubClient::listArtifacts(qint64 runId, Callback cb)
{
    request("GET", repoUrl(QStringLiteral("/actions/runs/%1/artifacts").arg(runId)), {}, std::move(cb));
}

void GitHubClient::getJobLogs(qint64 jobId, Callback cb)
{
    request("GET", repoUrl(QStringLiteral("/actions/jobs/%1/logs").arg(jobId)), {},
            [this, cb](const ApiReply& r) {
                if (r.status >= 300 && r.status < 400 && r.location.isValid())
                    request("GET", r.location, {}, cb, false);
                else
                    cb(r);
            });
}

void GitHubClient::downloadArtifact(qint64 artifactId, const QString& destFile, ProgressCb prog, DoneCb done)
{
    request("GET", repoUrl(QStringLiteral("/actions/artifacts/%1/zip").arg(artifactId)), {},
            [this, destFile, prog, done](const ApiReply& r) {
                if (r.status < 300 || r.status >= 400 || !r.location.isValid()) {
                    done(false, QStringLiteral("取得下載連結失敗 (HTTP %1) %2").arg(r.status).arg(r.error));
                    return;
                }
                auto file = std::make_shared<QFile>(destFile);
                if (!file->open(QIODevice::WriteOnly)) {
                    done(false, QStringLiteral("無法寫入 ") + destFile);
                    return;
                }
                QNetworkRequest req(r.location);
                req.setRawHeader("User-Agent", "VcpkgCompileTool");
                req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);
                QNetworkReply* rep = m_nam.get(req);
                m_download = rep;
                connect(rep, &QNetworkReply::readyRead, this, [rep, file]() { file->write(rep->readAll()); });
                connect(rep, &QNetworkReply::downloadProgress, this,
                        [prog](qint64 a, qint64 b) { prog(a, b); });
                connect(rep, &QNetworkReply::finished, this, [rep, file, destFile, done]() {
                    file->write(rep->readAll());
                    file->close();
                    const bool ok = rep->error() == QNetworkReply::NoError;
                    const QString err = rep->errorString();
                    rep->deleteLater();
                    if (!ok)
                        QFile::remove(destFile);
                    done(ok, err);
                });
            });
}

void GitHubClient::abortDownload()
{
    if (m_download)
        m_download->abort();
}
