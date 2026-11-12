#include "update/Updater.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace pauta {

namespace {

const char* kRepo = PAUTA_GITHUB_REPO; // "owner/name", set in CMakeLists.txt

QString currentVersion() { return QCoreApplication::applicationVersion(); }

QNetworkRequest githubRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Pauta/%1").arg(currentVersion()));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    return req;
}

// Readable Markdown in a QTextBrowser (Qt renders CommonMark).
QTextBrowser* notesView(const QString& markdown)
{
    auto* tb = new QTextBrowser;
    tb->setOpenExternalLinks(true);
    tb->setMarkdown(markdown);
    tb->document()->setDocumentMargin(12);
    tb->setMinimumSize(520, 320);
    return tb;
}

} // namespace

Updater::Updater(QWidget* window) : QObject(window), m_window(window), m_net(new QNetworkAccessManager(this)) {}

bool Updater::autoCheckEnabled() { return QSettings().value(QStringLiteral("update/auto"), true).toBool(); }
void Updater::setAutoCheckEnabled(bool on) { QSettings().setValue(QStringLiteral("update/auto"), on); }

int Updater::compareVersions(const QString& a, const QString& b)
{
    const QStringList pa = a.split(QLatin1Char('.')), pb = b.split(QLatin1Char('.'));
    for (int i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const int x = i < pa.size() ? pa[i].toInt() : 0;
        const int y = i < pb.size() ? pb[i].toInt() : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

void Updater::checkAtStartup()
{
    if (!autoCheckEnabled()) return;
    QSettings st;
    const QDateTime last = st.value(QStringLiteral("update/lastCheck")).toDateTime();
    if (last.isValid() && last.secsTo(QDateTime::currentDateTime()) < 20 * 3600) return;
    st.setValue(QStringLiteral("update/lastCheck"), QDateTime::currentDateTime());
    checkNow(false);
}

void Updater::checkNow(bool interactive)
{
    if (m_busy) return;
    m_busy = true;
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(QLatin1String(kRepo)));
    QNetworkReply* reply = m_net->get(githubRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, interactive] {
        reply->deleteLater();
        m_busy = false;
        if (reply->error() != QNetworkReply::NoError) {
            if (interactive)
                QMessageBox::warning(m_window, QStringLiteral("Verificar atualizações"),
                                     QStringLiteral("Não foi possível consultar as atualizações agora.\n\n%1").arg(reply->errorString()));
            return;
        }
        handleLatest(reply->readAll(), interactive);
    });
}

void Updater::handleLatest(const QByteArray& json, bool interactive)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    ReleaseInfo info;
    info.version = o.value(QStringLiteral("tag_name")).toString();
    if (info.version.startsWith(QLatin1Char('v'))) info.version.remove(0, 1);
    info.title = o.value(QStringLiteral("name")).toString();
    info.pageUrl = o.value(QStringLiteral("html_url")).toString();
    // Prefer the notes from the changelog section of that version; fall back to the release text.
    info.notes = o.value(QStringLiteral("body")).toString();

    // The package that fits this installation.
    QString wanted;
#if defined(Q_OS_WIN)
    wanted = QStringLiteral("Pauta.exe");
#elif defined(Q_OS_LINUX)
    wanted = qEnvironmentVariableIsSet("APPIMAGE") ? QStringLiteral(".AppImage") : QStringLiteral(".deb");
#endif
    for (const QJsonValue& a : o.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject ao = a.toObject();
        const QString name = ao.value(QStringLiteral("name")).toString();
        if (!wanted.isEmpty() && (name == wanted || (wanted.startsWith(QLatin1Char('.')) && name.endsWith(wanted)))) {
            info.assetName = name;
            info.assetUrl = ao.value(QStringLiteral("browser_download_url")).toString();
            info.assetSize = ao.value(QStringLiteral("size")).toInteger();
            break;
        }
    }

    if (info.version.isEmpty()) {
        if (interactive) QMessageBox::warning(m_window, QStringLiteral("Verificar atualizações"), QStringLiteral("Resposta inesperada do GitHub."));
        return;
    }
    const bool newer = compareVersions(info.version, currentVersion()) > 0;
    const bool skipped = QSettings().value(QStringLiteral("update/skip")).toString() == info.version;
    if (newer && (interactive || !skipped)) {
        offer(info);
    } else if (interactive) {
        QMessageBox::information(m_window, QStringLiteral("Verificar atualizações"),
                                 QStringLiteral("Você já está com a versão mais recente do Pauta (%1).").arg(currentVersion()));
    }
}

void Updater::offer(const ReleaseInfo& info)
{
    QDialog dlg(m_window);
    dlg.setWindowTitle(QStringLiteral("Atualização disponível"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* head = new QLabel(QStringLiteral("<span style='font-size:16px'><b>O Pauta %1 está disponível</b></span><br>"
                                           "Você está usando a versão %2. Veja o que mudou:")
                                .arg(info.version, currentVersion()));
    lay->addWidget(head);
    lay->addWidget(notesView(info.notes.isEmpty() ? QStringLiteral("_Sem descrição._") : info.notes), 1);
    auto* autoBox = new QCheckBox(QStringLiteral("Verificar novas versões ao abrir o Pauta"));
    autoBox->setChecked(autoCheckEnabled());
    lay->addWidget(autoBox);

    auto* bb = new QDialogButtonBox;
    QPushButton* now = bb->addButton(QStringLiteral("Atualizar agora"), QDialogButtonBox::AcceptRole);
    QPushButton* later = bb->addButton(QStringLiteral("Lembrar depois"), QDialogButtonBox::RejectRole);
    QPushButton* skip = bb->addButton(QStringLiteral("Pular esta versão"), QDialogButtonBox::DestructiveRole);
    now->setDefault(true);
    if (info.assetUrl.isEmpty()) now->setText(QStringLiteral("Abrir página de download"));
    lay->addWidget(bb);
    QAbstractButton* clicked = nullptr;
    connect(bb, &QDialogButtonBox::clicked, &dlg, [&](QAbstractButton* b) { clicked = b; dlg.accept(); });
    dlg.exec();
    setAutoCheckEnabled(autoBox->isChecked());
    Q_UNUSED(later);

    if (clicked == skip) {
        QSettings().setValue(QStringLiteral("update/skip"), info.version);
    } else if (clicked == now) {
        if (info.assetUrl.isEmpty()) QDesktopServices::openUrl(QUrl(info.pageUrl));
        else install(info);
    }
}

void Updater::install(const ReleaseInfo& info)
{
    // Where the download goes.
    QString target;
#if defined(Q_OS_WIN)
    target = QDir::temp().filePath(QStringLiteral("Pauta-%1-instalador.exe").arg(info.version));
#else
    if (info.assetName.endsWith(QStringLiteral(".AppImage")))
        target = qEnvironmentVariable("APPIMAGE") + QStringLiteral(".novo");
    else
        target = QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath(info.assetName);
#endif

    auto* progress = new QProgressDialog(QStringLiteral("Baixando o Pauta %1…").arg(info.version), QStringLiteral("Cancelar"), 0, 100, m_window);
    progress->setWindowTitle(QStringLiteral("Atualizando o Pauta"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);

    QNetworkReply* reply = m_net->get(githubRequest(QUrl(info.assetUrl)));
    connect(reply, &QNetworkReply::downloadProgress, progress, [progress, info](qint64 got, qint64 total) {
        if (total <= 0) total = info.assetSize;
        if (total > 0) progress->setValue(int(got * 100 / total));
    });
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, progress, target, info] {
        reply->deleteLater();
        progress->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() != QNetworkReply::OperationCanceledError)
                QMessageBox::warning(m_window, QStringLiteral("Atualizar"), QStringLiteral("O download falhou: %1").arg(reply->errorString()));
            return;
        }
        QSaveFile f(target);
        if (!f.open(QIODevice::WriteOnly) || f.write(reply->readAll()) < 0 || !f.commit()) {
            QMessageBox::warning(m_window, QStringLiteral("Atualizar"), QStringLiteral("Não foi possível salvar %1.").arg(target));
            return;
        }

#if defined(Q_OS_WIN)
        // Silent install over the current one; the installer reopens Pauta when it finishes.
        if (beforeQuit && !beforeQuit()) return;
        const QStringList args{ QStringLiteral("/SILENT"), QStringLiteral("/SP-"), QStringLiteral("/NOCANCEL"),
                                QStringLiteral("/CLOSEAPPLICATIONS"), QStringLiteral("/NORESTART") };
        if (!QProcess::startDetached(target, args)) {
            QMessageBox::warning(m_window, QStringLiteral("Atualizar"), QStringLiteral("Não foi possível iniciar o instalador."));
            return;
        }
        emit quitForUpdate();
#else
        if (info.assetName.endsWith(QStringLiteral(".AppImage"))) {
            // Replace the running AppImage file and start the new one.
            if (beforeQuit && !beforeQuit()) return;
            const QString current = qEnvironmentVariable("APPIMAGE");
            QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
            QFile::remove(current);
            if (!QFile::rename(target, current)) {
                QMessageBox::warning(m_window, QStringLiteral("Atualizar"), QStringLiteral("Não foi possível substituir %1.").arg(current));
                return;
            }
            QProcess::startDetached(current, {});
            emit quitForUpdate();
        } else {
            // .deb: the system's software center finishes the installation.
            QDesktopServices::openUrl(QUrl::fromLocalFile(target));
            QMessageBox::information(m_window, QStringLiteral("Atualizar"),
                                     QStringLiteral("O pacote foi baixado em %1 e aberto na Central de Aplicativos.\n"
                                                    "Clique em Instalar e depois abra o Pauta de novo.").arg(target));
        }
#endif
    });
}

QString Updater::changelogFor(const QString& version)
{
    QFile f(QStringLiteral(":/CHANGELOG.md"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QString all = QString::fromUtf8(f.readAll());
    const QString marker = QStringLiteral("## [%1]").arg(version);
    const qsizetype start = all.indexOf(marker);
    if (start < 0) return {};
    qsizetype end = all.indexOf(QStringLiteral("\n## ["), start + marker.size());
    if (end < 0) end = all.size();
    return all.mid(start, end - start).trimmed();
}

void Updater::showWhatsNew(QWidget* parent, const QString& version)
{
    QDialog* dlg = whatsNewDialog(parent, version);
    dlg->exec();
    delete dlg;
}

QDialog* Updater::whatsNewDialog(QWidget* parent, const QString& version)
{
    QString notes = changelogFor(version).section(QChar(u'\n'), 1).trimmed(); // the title already names the version
    if (notes.isEmpty()) notes = QStringLiteral("_Sem notas para esta versão._");
    auto* dlgPtr = new QDialog(parent);
    QDialog& dlg = *dlgPtr;
    dlg.setWindowTitle(QStringLiteral("O que há de novo"));
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(QStringLiteral("<span style='font-size:16px'><b>Novidades do Pauta %1</b></span>").arg(version)));
    lay->addWidget(notesView(notes), 1);
    auto* all = new QLabel(QStringLiteral("<a href='https://github.com/%1/blob/main/CHANGELOG.md'>Ver o histórico completo</a>")
                               .arg(QLatin1String(kRepo)));
    all->setOpenExternalLinks(true);
    lay->addWidget(all);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(bb);
    return dlgPtr;
}

} // namespace pauta
