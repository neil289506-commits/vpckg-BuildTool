#pragma once
#include <QObject>
#include <QStringList>
#include <functional>

class QProcess;

// 本機 vcpkg：首次啟動 git clone + bootstrap 取得 vcpkg.exe，並提供 `vcpkg search` 候補。
class VcpkgManager : public QObject {
    Q_OBJECT
public:
    explicit VcpkgManager(QObject* parent = nullptr);

    QString root() const;
    QString exePath() const;
    bool isReady() const;

    void ensureReady();
    void search(const QString& query);

signals:
    void log(const QString& line);
    void readyChanged(bool ok, const QString& message);
    void searchFinished(const QString& query, const QStringList& names);

private:
    void startProc(const QString& program, const QStringList& args, const QString& workDir,
                   std::function<void(bool)> onDone);
    void runBootstrap();
    void emitLines(const QString& text);

    QProcess* m_proc = nullptr;
    QProcess* m_search = nullptr;
};
