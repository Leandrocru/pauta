#pragma once
// Checks GitHub Releases for a newer Pauta, shows its notes and installs it on request.
// Only the program version is sent (as the User-Agent); nothing about the user or their scores.
#include <QObject>
#include <QString>

#include <functional>

class QDialog;
class QNetworkAccessManager;
class QWidget;

namespace pauta {

struct ReleaseInfo {
    QString version;   // "0.7.0"
    QString title;
    QString notes;     // Markdown
    QString pageUrl;
    QString assetName;
    QString assetUrl;  // download for this system ("" = only the release page)
    qint64 assetSize = 0;
};

class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QWidget* window);

    // Called right before the program is replaced; return false to cancel (e.g. "save?" cancelled).
    std::function<bool()> beforeQuit;

    static bool autoCheckEnabled();
    static void setAutoCheckEnabled(bool on);
    static int compareVersions(const QString& a, const QString& b); // <0, 0, >0

    void checkAtStartup();           // respects the setting; at most once a day
    void checkNow(bool interactive); // interactive: also reports "up to date" and errors

    // Notes of one version taken from the CHANGELOG embedded in the program.
    static QString changelogFor(const QString& version);
    static void showWhatsNew(QWidget* parent, const QString& version);
    static QDialog* whatsNewDialog(QWidget* parent, const QString& version); // caller owns it

signals:
    void quitForUpdate(); // the window should close (asking to save) so the installer can run

private:
    void handleLatest(const QByteArray& json, bool interactive);
    void offer(const ReleaseInfo& info);
    void install(const ReleaseInfo& info);

    QWidget* m_window;
    QNetworkAccessManager* m_net;
    bool m_busy = false;
};

} // namespace pauta
