#include "io/MusicXml.h"
#include "update/Updater.h"

#include <QDialog>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include "playback/MidiOut.h"
#include "playback/Sequence.h"
#include "io/NativeFormat.h"
#include "render/Engraver.h"
#include "render/Renderer.h"
#include "render/Smufl.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGlyphRun>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QKeySequence>

#include "edit/Editor.h"
#include "ui/ScoreView.h"
#include <QLibraryInfo>
#include <QLocale>
#include <QRandomGenerator>
#include <QTextStream>
#include <QTranslator>

using namespace pauta;

namespace {

// Application icon: white treble clef on a blue rounded square, over faint staff lines.
QImage makeAppIcon(int size)
{
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal r = size * 0.18;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x1e, 0x5f, 0xb4));
    p.drawRoundedRect(QRectF(size * 0.03, size * 0.03, size * 0.94, size * 0.94), r, r);
    p.setPen(QPen(QColor(255, 255, 255, 90), size * 0.018));
    for (int i = 0; i < 5; ++i) {
        const qreal y = size * (0.34 + i * 0.09);
        p.drawLine(QPointF(size * 0.12, y), QPointF(size * 0.88, y));
    }
    const auto& font = smufl::Font::instance();
    const qreal sp = size * 0.09;
    const QRectF bb = font.bbox(smufl::GClef, sp);
    QGlyphRun run;
    run.setRawFont(font.rawFont(sp));
    run.setGlyphIndexes({ font.glyphIndex(smufl::GClef) });
    // The clef's curl wraps the second line (G line) of the faint staff.
    run.setPositions({ QPointF(size * 0.5 - bb.center().x(), size * (0.34 + 3 * 0.09)) });
    p.setPen(Qt::white);
    p.drawGlyphRun(QPointF(0, 0), run);
    p.end();
    return img;
}

// Large synthetic score for performance measurements.
Score makeBenchScore(int measures)
{
    Score s = Score::makeEmpty(QStringLiteral("Benchmark"),
                               { { QStringLiteral("Piano"), Clef::Treble }, { QString(), Clef::Bass } },
                               TimeSig{ 4, 4 }, 2, measures);
    QRandomGenerator rng(42);
    for (size_t st = 0; st < 2; ++st) {
        const int base = st == 0 ? 30 : 18;
        for (Measure& m : s.staves[st].measures) {
            Frac t;
            while (t < Frac(1)) {
                Event e;
                e.durLog = 2 + int(rng.bounded(3));
                while (t + e.duration() > Frac(1)) ++e.durLog;
                const int n = 1 + int(rng.bounded(st == 0 ? 2 : 3));
                for (int k = 0; k < n; ++k)
                    e.pitches.push_back(Pitch::fromDiatonic(base + int(rng.bounded(10)), int(rng.bounded(5)) == 0 ? 1 : 0));
                e.sortPitches();
                t += e.duration();
                m.events.push_back(std::move(e));
            }
        }
    }
    return s;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Pauta"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));
    QApplication::setOrganizationName(QStringLiteral("Pauta"));

    QApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/pauta.png")));

    // Qt's own dialogs in Portuguese: next to the executable (installed/portable) or from Qt.
    QTranslator qtTr;
    if (qtTr.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"), QCoreApplication::applicationDirPath() + QStringLiteral("/translations"))
        || qtTr.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTr);

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Pauta — editor de partituras"));
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption pdfOpt(QStringLiteral("export-pdf"), QStringLiteral("Exporta para PDF e sai."), QStringLiteral("arquivo.pdf"));
    QCommandLineOption pngOpt(QStringLiteral("export-png"), QStringLiteral("Exporta a 1ª página para PNG e sai."), QStringLiteral("arquivo.png"));
    QCommandLineOption benchOpt(QStringLiteral("bench"), QStringLiteral("Mede o tempo de gravação de N compassos e sai."), QStringLiteral("N"));
    QCommandLineOption emptyOpt(QStringLiteral("empty"), QStringLiteral("Começa com uma partitura de piano vazia."));
    QCommandLineOption keysOpt(QStringLiteral("test-keys"), QStringLiteral("(teste) Digita uma sequência de teclas."), QStringLiteral("teclas"));
    QCommandLineOption grabOpt(QStringLiteral("grab"), QStringLiteral("(teste) Salva uma captura da janela e sai."), QStringLiteral("arquivo.png"));
    QCommandLineOption savePauta(QStringLiteral("save-to"), QStringLiteral("(teste) Com --grab, salva a partitura."), QStringLiteral("arquivo.pauta"));
    QCommandLineOption toolOpt(QStringLiteral("tool"), QStringLiteral("(teste) Ferramenta ativa (índice)."), QStringLiteral("n"));
    QCommandLineOption scrollOpt(QStringLiteral("scroll-view"), QStringLiteral("(teste) Visualização contínua."));
    QCommandLineOption selectOpt(QStringLiteral("select-all"), QStringLiteral("(teste) Seleciona tudo."));
    QCommandLineOption mouseOpt(QStringLiteral("test-mouse"),
        QStringLiteral("(teste) Ações de mouse: \"move|click:compasso:alvo:altura\" (alvo = fim ou índice da nota; altura = diatônico, 34 = Si4)."),
        QStringLiteral("acoes"));
    QCommandLineOption whatsNewGrabOpt(QStringLiteral("grab-whats-new"), QStringLiteral("(teste) Captura a janela de novidades."), QStringLiteral("arquivo.png"));
    QCommandLineOption mxlOutOpt(QStringLiteral("export-musicxml"), QStringLiteral("Exporta para MusicXML (.musicxml ou .mxl) e sai."), QStringLiteral("arquivo"));
    QCommandLineOption updCheckOpt(QStringLiteral("update-check"), QStringLiteral("(teste) Consulta a última versão no GitHub e sai."));
    QCommandLineOption playCheckOpt(QStringLiteral("playback-check"), QStringLiteral("(teste) Monta a sequência de reprodução e abre a saída de som, sem tocar."));
    QCommandLineOption iconOpt(QStringLiteral("write-icon"), QStringLiteral("(desenvolvimento) Gera o ícone do programa."), QStringLiteral("pasta"));
    cli.addOptions({ pdfOpt, pngOpt, benchOpt, emptyOpt, keysOpt, grabOpt, savePauta, toolOpt, scrollOpt, selectOpt, iconOpt, playCheckOpt, mouseOpt, updCheckOpt, whatsNewGrabOpt, mxlOutOpt });
    cli.addPositionalArgument(QStringLiteral("partitura"), QStringLiteral("Arquivo .pauta ou MusicXML a abrir."));
    cli.process(app);

    if (!smufl::Font::instance().isValid()) {
        qCritical("Fonte musical Bravura não encontrada nos recursos.");
        return 2;
    }

    QTextStream out(stdout);
    if (cli.isSet(iconOpt)) {
        // Development helper: regenerates resources/pauta.png and pauta.ico.
        const QString dir = cli.value(iconOpt);
        const QImage img = makeAppIcon(256);
        const bool ok = img.save(dir + QStringLiteral("/pauta.png")) && img.save(dir + QStringLiteral("/pauta.ico"));
        out << (ok ? "ok\n" : "falhou\n");
        return ok ? 0 : 1;
    }
    if (cli.isSet(whatsNewGrabOpt)) {
        QDialog* d = Updater::whatsNewDialog(nullptr, QApplication::applicationVersion());
        d->resize(620, 460);
        d->show();
        QApplication::processEvents();
        d->grab().save(cli.value(whatsNewGrabOpt));
        delete d;
        return 0;
    }
    if (cli.isSet(updCheckOpt)) {
        QNetworkAccessManager net;
        QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(QLatin1String(PAUTA_GITHUB_REPO))));
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Pauta/%1").arg(QApplication::applicationVersion()));
        QNetworkReply* r = net.get(req);
        QEventLoop loop;
        QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        if (r->error() != QNetworkReply::NoError) {
            out << "erro: " << r->errorString() << Qt::endl;
            return 1;
        }
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        const QString tag = o.value(QStringLiteral("tag_name")).toString();
        out << "atual=" << QApplication::applicationVersion() << " ultima=" << tag
            << " mais_nova=" << (Updater::compareVersions(tag.mid(1), QApplication::applicationVersion()) > 0 ? "sim" : "nao")
            << " pacotes=" << o.value(QStringLiteral("assets")).toArray().size()
            << " notas_embutidas=" << (Updater::changelogFor(QApplication::applicationVersion()).isEmpty() ? "nao" : "sim") << Qt::endl;
        return 0;
    }
    if (cli.isSet(playCheckOpt)) {
        Score s = Score::makeDemo();
        QString err;
        if (!cli.positionalArguments().isEmpty() && !io::loadAnyScore(s, cli.positionalArguments().first(), &err)) {
            out << "falhou: " << err << Qt::endl;
            return 1;
        }
        const playback::Sequence seq = playback::buildSequence(s, 0);
        auto midi = playback::createMidiOut();
        QStringList vels;
        for (const playback::MidiMsg& m : seq.messages)
            if ((m.status & 0xF0) == 0x90) vels << QString::number(m.data2);
        out << "volumes=" << vels.join(QLatin1Char(',')) << Qt::endl;
        out << "notas=" << seq.noteCount << " mensagens=" << seq.messages.size()
            << " duracao=" << QString::number(seq.length, 'f', 2) << "s compassos=" << seq.measureStart.size()
            << " saida=\"" << midi->name() << "\" ok=" << (midi->ok() ? "sim" : "nao") << Qt::endl;
        return 0;
    }
    if (cli.isSet(benchOpt)) {
        const int n = std::max(1, cli.value(benchOpt).toInt());
        const Score s = makeBenchScore(n);
        QElapsedTimer t;
        t.start();
        Layout l = engrave(s);
        const double first = t.nsecsElapsed() / 1e6;
        t.restart();
        const int runs = 5;
        for (int i = 0; i < runs; ++i) l = engrave(s);
        out << "compassos=" << n << " paginas=" << l.pages.size()
            << " primeira=" << QString::number(first, 'f', 1) << "ms"
            << " media=" << QString::number(t.nsecsElapsed() / 1e6 / runs, 'f', 1) << "ms\n";
        return 0;
    }

    const QStringList args = cli.positionalArguments();
    if (cli.isSet(mxlOutOpt)) {
        Score s = Score::makeDemo();
        QString err;
        if (!args.isEmpty() && !io::loadAnyScore(s, args.first(), &err)) {
            out << "falhou: " << err << Qt::endl;
            return 1;
        }
        const bool ok = io::exportMusicXml(s, cli.value(mxlOutOpt), &err);
        out << (ok ? QStringLiteral("ok") : QStringLiteral("falhou: ") + err) << Qt::endl;
        return ok ? 0 : 1;
    }
    if (cli.isSet(pdfOpt) || cli.isSet(pngOpt)) {
        Score s = Score::makeDemo();
        QString err;
        if (!args.isEmpty() && !io::loadAnyScore(s, args.first(), &err)) {
            qCritical("%s", qPrintable(err));
            return 1;
        }
        const Layout l = engrave(s);
        bool ok = true;
        if (cli.isSet(pdfOpt)) ok &= exportPdf(l, cli.value(pdfOpt), s.title);
        if (cli.isSet(pngOpt)) ok &= exportPng(l, 0, cli.value(pngOpt));
        out << (ok ? "ok\n" : "falhou\n");
        return ok ? 0 : 1;
    }

    MainWindow w;
    if (!args.isEmpty()) w.openFile(args.first());
    else if (cli.isSet(emptyOpt))
        w.editor()->setScore(Score::makeEmpty(QStringLiteral("Teste"),
                                              { { QStringLiteral("Piano"), Clef::Treble }, { QString(), Clef::Bass } },
                                              TimeSig{ 4, 4 }, 0, 8));
    const bool testMode = cli.isSet(keysOpt) || cli.isSet(grabOpt) || cli.isSet(mouseOpt);
    if (testMode) {
        w.setTool(cli.isSet(toolOpt) ? Tool(cli.value(toolOpt).toInt()) : Tool::SpeedyEntry);
        if (cli.isSet(scrollOpt)) w.view()->setContinuous(true);
    }
    w.show();
    if (!testMode && args.isEmpty() && !cli.isSet(emptyOpt)) w.runLaunchWindow();
    if (!testMode) w.startupChecks();

    // Test hooks: type a key sequence into the score view and/or save a screenshot of the window.
    if (testMode) {
        QApplication::processEvents();
        if (cli.isSet(selectOpt)) w.editor()->selectAll();
        // Mouse actions on staff 1 of a measure, aimed at the end of the layer or at a note.
        for (const QString& act : cli.value(mouseOpt).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const QStringList f = act.split(QLatin1Char(':'));
            if (f.size() != 4) continue;
            ScoreView* view = w.view();
            const MeasureSlot* slot = view->layout().slot(0, f[1].toInt() - 1);
            if (!slot) continue;
            const int voice = w.editor()->cursor().voice;
            const qreal sp = view->layout().sp;
            const qreal px = f[2] == u"fim" ? slot->appendX[size_t(voice)] + 0.6 * sp
                                            : slot->eventX[size_t(voice)][size_t(f[2].toInt())] + 0.6 * sp;
            const qreal py = view->layout().yForPosition(*slot, staffPosition(f[3].toInt(), slot->clef));
            const QPointF wp = view->pageRect(slot->page).topLeft() + QPointF(px, py) * view->zoom();
            const QPointF gp = view->mapToGlobal(wp);
            QMouseEvent mv(QEvent::MouseMove, wp, gp, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(view, &mv);
            if (f[0] == u"click") {
                QMouseEvent pr(QEvent::MouseButtonPress, wp, gp, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(view, &pr);
                QMouseEvent rl(QEvent::MouseButtonRelease, wp, gp, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(view, &rl);
                QMouseEvent mv2(QEvent::MouseMove, wp, gp, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(view, &mv2); // as a real mouse would keep hovering there
            }
            QApplication::processEvents();
        }
        if (cli.isSet(keysOpt)) {
            for (const QString& tok : cli.value(keysOpt).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                const QKeySequence seq = QKeySequence::fromString(tok, QKeySequence::PortableText);
                if (seq.isEmpty()) continue;
                const QKeyCombination kc = seq[0];
                const QString text = tok.size() == 1 ? tok.toLower() : QString();
                QKeyEvent press(QEvent::KeyPress, kc.key(), kc.keyboardModifiers(), text);
                QApplication::sendEvent(w.view(), &press);
            }
            QApplication::processEvents();
        }
        if (cli.isSet(grabOpt)) {
            w.grab().save(cli.value(grabOpt));
            if (cli.isSet(savePauta)) io::saveNative(w.editor()->score(), cli.value(savePauta));
            return 0;
        }
    }
    return app.exec();
}
