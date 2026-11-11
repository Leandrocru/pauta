#include "io/NativeFormat.h"
#include "render/Engraver.h"
#include "render/Renderer.h"
#include "render/Smufl.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QKeyEvent>
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

    QTranslator qtTr;
    if (qtTr.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
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
    cli.addOptions({ pdfOpt, pngOpt, benchOpt, emptyOpt, keysOpt, grabOpt, savePauta });
    cli.addPositionalArgument(QStringLiteral("partitura"), QStringLiteral("Arquivo .pauta a abrir."));
    cli.process(app);

    if (!smufl::Font::instance().isValid()) {
        qCritical("Fonte musical Bravura não encontrada nos recursos.");
        return 2;
    }

    QTextStream out(stdout);
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
    if (cli.isSet(pdfOpt) || cli.isSet(pngOpt)) {
        Score s = Score::makeDemo();
        QString err;
        if (!args.isEmpty() && !io::loadNative(s, args.first(), &err)) {
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
    w.show();

    // Test hooks: type a key sequence into the score view and/or save a screenshot of the window.
    if (cli.isSet(keysOpt) || cli.isSet(grabOpt)) {
        QApplication::processEvents();
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
