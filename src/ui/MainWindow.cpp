#include "ui/MainWindow.h"

#include "edit/Editor.h"
#include "io/NativeFormat.h"
#include "render/Renderer.h"
#include "render/Smufl.h"
#include "ui/NewScoreDialog.h"
#include "ui/ScoreView.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGlyphRun>
#include <QPainter>
#include <QPixmap>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QToolBar>

namespace pauta {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    m_editor = new Editor(this);
    m_view = new ScoreView(m_editor);
    m_scroll = new QScrollArea;
    m_scroll->setWidget(m_view);
    m_scroll->setAlignment(Qt::AlignHCenter);
    m_scroll->setBackgroundRole(QPalette::Dark);
    setCentralWidget(m_scroll);

    m_status = new QLabel;
    m_perf = new QLabel;
    m_perf->setStyleSheet(QStringLiteral("color: gray"));
    statusBar()->addWidget(m_status, 1);
    statusBar()->addPermanentWidget(m_perf);

    buildMenus();
    buildPalette();

    connect(m_view, &ScoreView::ensureVisibleRequested, this, [this](const QRect& r) {
        m_scroll->ensureVisible(r.center().x(), r.center().y(), r.width() / 2 + 40, r.height() / 2 + 40);
    });
    connect(m_view, &ScoreView::layoutTimed, this, [this](double ms) {
        m_perf->setText(QStringLiteral("Gravação: %1 ms · %2 página(s)")
                            .arg(ms, 0, 'f', 1)
                            .arg(m_view->layout().pages.size()));
    });
    connect(m_editor, &Editor::cursorChanged, this, &MainWindow::updateStatus);
    connect(m_editor, &Editor::message, this, [this](const QString& t) {
        statusBar()->showMessage(t, 4000);
        QApplication::beep();
    });
    connect(m_editor->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);

    m_perf->setText(QStringLiteral("Gravação: %1 ms").arg(m_view->lastLayoutMs(), 0, 'f', 1));
    resize(1200, 860);
    updateTitle();
    updateStatus();
    m_view->setFocus();
}

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(QStringLiteral("&Arquivo"));
    file->addAction(QStringLiteral("&Nova partitura…"), QKeySequence::New, this, &MainWindow::newScore);
    file->addAction(QStringLiteral("&Abrir…"), QKeySequence::Open, this, &MainWindow::open);
    file->addSeparator();
    file->addAction(QStringLiteral("&Salvar"), QKeySequence::Save, this, &MainWindow::save);
    file->addAction(QStringLiteral("Salvar &como…"), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    file->addSeparator();
    file->addAction(QStringLiteral("Exportar &PDF…"), QKeySequence(QStringLiteral("Ctrl+Shift+E")), this, &MainWindow::exportPdf);
    file->addSeparator();
    file->addAction(QStringLiteral("Sai&r"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* edit = menuBar()->addMenu(QStringLiteral("&Editar"));
    QAction* undo = m_editor->undoStack()->createUndoAction(this, QStringLiteral("Desfazer"));
    undo->setShortcut(QKeySequence::Undo);
    QAction* redo = m_editor->undoStack()->createRedoAction(this, QStringLiteral("Refazer"));
    redo->setShortcuts({ QKeySequence::Redo, QKeySequence(QStringLiteral("Ctrl+Y")) });
    edit->addAction(undo);
    edit->addAction(redo);

    QMenu* meas = menuBar()->addMenu(QStringLiteral("&Compasso"));
    meas->addAction(QStringLiteral("&Adicionar compassos…"), QKeySequence(QStringLiteral("Ctrl+Shift+M")), this, [this] {
        bool ok = false;
        const int n = QInputDialog::getInt(this, QStringLiteral("Adicionar compassos"),
                                           QStringLiteral("Quantos compassos adicionar ao final?"), 4, 1, 1000, 1, &ok);
        if (ok) m_editor->addMeasures(n);
    });
    meas->addAction(QStringLiteral("&Excluir compasso atual"), this, [this] { m_editor->deleteMeasure(); });

    QMenu* view = menuBar()->addMenu(QStringLiteral("E&xibir"));
    view->addAction(QStringLiteral("Aumentar zoom"), QKeySequence::ZoomIn, this, [this] { m_view->setZoom(m_view->zoom() * 1.15); });
    view->addAction(QStringLiteral("Diminuir zoom"), QKeySequence::ZoomOut, this, [this] { m_view->setZoom(m_view->zoom() / 1.15); });
    view->addAction(QStringLiteral("Zoom 100%"), QKeySequence(QStringLiteral("Ctrl+0")), this,
                    [this] { m_view->setZoom(logicalDpiX() / 72.0); });

    QMenu* help = menuBar()->addMenu(QStringLiteral("A&juda"));
    help->addAction(QStringLiteral("&Atalhos de entrada de notas"), QKeySequence::HelpContents, this, &MainWindow::showShortcuts);
    help->addAction(QStringLiteral("&Sobre o Pauta"), this, [this] {
        QMessageBox::about(this, QStringLiteral("Sobre o Pauta"),
                           QStringLiteral("<b>Pauta</b> %1<br>Editor de partituras leve, inspirado no fluxo de trabalho do Finale.<br><br>"
                                          "Fonte musical Bravura © Steinberg Media Technologies (SIL Open Font License).")
                               .arg(QApplication::applicationVersion()));
    });
}

namespace {

// Renders one SMuFL glyph centred in a square icon (tight bounding box, so toolbars stay compact).
QIcon glyphIcon(char16_t cp, int size = 32)
{
    const auto& font = smufl::Font::instance();
    const qreal sp = size / 8.0;
    QRectF bb = font.bbox(cp, sp);
    const qreal scale = std::min(1.0, (size - 4) / std::max(bb.width(), bb.height()));
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(size / 2.0, size / 2.0);
    p.scale(scale, scale);
    QGlyphRun run;
    run.setRawFont(font.rawFont(sp));
    run.setGlyphIndexes({ font.glyphIndex(cp) });
    run.setPositions({ QPointF(-bb.center().x(), -bb.center().y()) });
    p.setPen(QColor(30, 30, 30));
    p.drawGlyphRun(QPointF(0, 0), run);
    p.end();
    return QIcon(pm);
}

} // namespace

void MainWindow::buildPalette()
{
    QToolBar* tb = addToolBar(QStringLiteral("Paleta"));
    tb->setMovable(false);
    tb->setFocusPolicy(Qt::NoFocus);
    tb->setIconSize(QSize(28, 28));

    m_durGroup = new QActionGroup(this);
    struct D { int log; const char* key; };
    const D durs[] = { { 0, "7" }, { 1, "6" }, { 2, "5" }, { 3, "4" }, { 4, "3" }, { 5, "2" } };
    for (const D& d : durs) {
        QAction* a = tb->addAction(glyphIcon(smufl::metronomeNoteFor(d.log)), durationName(d.log));
        a->setCheckable(true);
        a->setData(d.log);
        a->setToolTip(QStringLiteral("%1 (tecla %2)").arg(durationName(d.log), QString::fromLatin1(d.key)));
        m_durGroup->addAction(a);
        if (d.log == m_editor->durLog()) a->setChecked(true);
        connect(a, &QAction::triggered, this, [this, d] {
            if (m_editor->currentEvent()) m_editor->enterDuration(d.log);
            else m_editor->setDurLog(d.log);
            m_view->setFocus();
        });
    }
    tb->addSeparator();
    auto addTool = [&](char16_t glyph, const QString& tip, auto fn) {
        QAction* a = tb->addAction(glyphIcon(glyph), tip);
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, [this, fn] { fn(); m_view->setFocus(); });
    };
    addTool(smufl::RestQuarter, QStringLiteral("Pausa (R)"), [this] { m_editor->enterRest(); });
    addTool(smufl::AugmentationDot, QStringLiteral("Ponto de aumento (.)"), [this] { m_editor->toggleDot(); });
    addTool(smufl::AccFlat, QStringLiteral("Bemol (-)"), [this] { m_editor->alter(-1); });
    addTool(smufl::AccNatural, QStringLiteral("Bequadro (=)"), [this] { m_editor->naturalize(); });
    addTool(smufl::AccSharp, QStringLiteral("Sustenido (+)"), [this] { m_editor->alter(1); });
}

void MainWindow::updateStatus()
{
    const Cursor& c = m_editor->cursor();
    const Score& s = m_editor->score();
    if (s.staves.empty()) return;
    const Staff& st = s.staves[size_t(c.staff)];
    QString staffName = st.name.isEmpty() ? QStringLiteral("Pauta %1").arg(c.staff + 1) : st.name;
    Pitch p = Pitch::fromDiatonic(c.diatonic, keyAlterFor(((c.diatonic % 7) + 7) % 7, s.keyFifths));
    m_status->setText(QStringLiteral("%1  ·  Compasso %2 de %3  ·  Cursor: %4  ·  Duração: %5")
                          .arg(staffName)
                          .arg(c.measure + 1)
                          .arg(s.measureCount())
                          .arg(pitchName(p), durationName(m_editor->durLog())));
    if (m_durGroup)
        for (QAction* a : m_durGroup->actions())
            if (a->data().toInt() == m_editor->durLog()) a->setChecked(true);
}

void MainWindow::updateTitle()
{
    const QString name = m_path.isEmpty() ? QStringLiteral("Sem título") : QFileInfo(m_path).fileName();
    setWindowTitle(QStringLiteral("%1[*] — Pauta").arg(name));
    setWindowModified(!m_editor->undoStack()->isClean());
}

bool MainWindow::maybeSave()
{
    if (m_editor->undoStack()->isClean()) return true;
    const auto r = QMessageBox::question(this, QStringLiteral("Pauta"),
                                         QStringLiteral("A partitura tem alterações não salvas. Deseja salvar?"),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Save) return save();
    return r == QMessageBox::Discard;
}

bool MainWindow::save()
{
    if (m_path.isEmpty()) return saveAs();
    QString err;
    if (!io::saveNative(m_editor->score(), m_path, &err)) {
        QMessageBox::warning(this, QStringLiteral("Erro ao salvar"), err);
        return false;
    }
    m_editor->undoStack()->setClean();
    statusBar()->showMessage(QStringLiteral("Salvo em %1").arg(m_path), 3000);
    updateTitle();
    return true;
}

bool MainWindow::saveAs()
{
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Salvar partitura"), m_path,
                                                QStringLiteral("Partitura Pauta (*.pauta)"));
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".pauta"), Qt::CaseInsensitive)) path += QStringLiteral(".pauta");
    m_path = path;
    return save();
}

void MainWindow::newScore()
{
    if (!maybeSave()) return;
    NewScoreDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    m_editor->setScore(dlg.result());
    m_path.clear();
    updateTitle();
    m_view->setFocus();
}

void MainWindow::open()
{
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Abrir partitura"), QString(),
                                                      QStringLiteral("Partitura Pauta (*.pauta)"));
    if (!path.isEmpty()) openFile(path);
}

bool MainWindow::openFile(const QString& path)
{
    Score s;
    QString err;
    if (!io::loadNative(s, path, &err)) {
        QMessageBox::warning(this, QStringLiteral("Erro ao abrir"), err);
        return false;
    }
    m_editor->setScore(std::move(s));
    m_path = path;
    updateTitle();
    return true;
}

void MainWindow::exportPdf()
{
    QString base = m_path.isEmpty() ? QStringLiteral("partitura.pdf") : QFileInfo(m_path).completeBaseName() + QStringLiteral(".pdf");
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Exportar PDF"), base, QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    if (!pauta::exportPdf(m_view->layout(), path, m_editor->score().title))
        QMessageBox::warning(this, QStringLiteral("Erro"), QStringLiteral("Não foi possível criar o PDF."));
    else
        statusBar()->showMessage(QStringLiteral("PDF exportado: %1").arg(path), 4000);
}

void MainWindow::showShortcuts()
{
    QMessageBox::information(this, QStringLiteral("Entrada de notas (estilo Speedy Entry)"), QStringLiteral(
        "<table cellpadding=3>"
        "<tr><td><b>↑ / ↓</b></td><td>Move o cursor de altura (Ctrl: oitava)</td></tr>"
        "<tr><td><b>← / →</b></td><td>Nota anterior / próxima (Ctrl: compasso)</td></tr>"
        "<tr><td><b>Alt + ↑ / ↓</b></td><td>Pauta de cima / de baixo</td></tr>"
        "<tr><td><b>1 – 8</b></td><td>Insere nota na altura do cursor: 7 semibreve, 6 mínima, 5 semínima, "
        "4 colcheia, 3 semicolcheia, 2 fusa, 1 semifusa, 8 breve. Sobre uma nota existente, muda a duração.</td></tr>"
        "<tr><td><b>A – G</b></td><td>Insere a nota pelo nome (C=Dó … B=Si), na oitava mais próxima</td></tr>"
        "<tr><td><b>Enter</b></td><td>Insere nota / adiciona ou remove a nota do cursor no acorde</td></tr>"
        "<tr><td><b>R</b></td><td>Insere pausa / alterna nota ↔ pausa</td></tr>"
        "<tr><td><b>+ / − / =</b></td><td>Sustenido / bemol / bequadro</td></tr>"
        "<tr><td><b>.</b></td><td>Ponto de aumento</td></tr>"
        "<tr><td><b>Shift + ↑ / ↓</b></td><td>Transpõe a nota (Ctrl: oitava)</td></tr>"
        "<tr><td><b>Delete / Backspace</b></td><td>Apaga a nota atual / anterior</td></tr>"
        "<tr><td><b>Ctrl + roda do mouse</b></td><td>Zoom</td></tr>"
        "</table>"));
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (maybeSave()) e->accept();
    else e->ignore();
}

} // namespace pauta
