#include "ui/MainWindow.h"

#include "edit/Editor.h"
#include "playback/Player.h"
#include "io/MusicXml.h"
#include "io/NativeFormat.h"
#include "render/Engraver.h"
#include "render/Renderer.h"
#include "render/Smufl.h"
#include "ui/Icons.h"
#include "ui/LaunchDialog.h"
#include "ui/NewScoreDialog.h"
#include "ui/ScoreView.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

namespace pauta {

namespace {

QString soon(const QString& s) { return s + QStringLiteral(" (em breve)"); }

QAction* disabledAction(QMenu* m, const QString& text)
{
    QAction* a = m->addAction(soon(text));
    a->setEnabled(false);
    return a;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    m_editor = new Editor(this);
    m_player = new playback::Player(this);
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
    buildToolPalette();
    buildSimplePalette();
    buildNavigationBar();

    connect(m_view, &ScoreView::ensureVisibleRequested, this, [this](const QRect& r) {
        m_scroll->ensureVisible(r.center().x(), r.center().y(), r.width() / 2 + 40, r.height() / 2 + 40);
    });
    connect(m_view, &ScoreView::scrollByRequested, this, [this](int dx, int dy) {
        m_scroll->horizontalScrollBar()->setValue(m_scroll->horizontalScrollBar()->value() + dx);
        m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->value() + dy);
    });
    connect(m_view, &ScoreView::layoutTimed, this, [this](double ms) {
        m_perf->setText(QStringLiteral("Gravação: %1 ms").arg(ms, 0, 'f', 1));
        updateNavigation();
    });
    connect(m_view, &ScoreView::zoomChanged, this, [this] { updateNavigation(); });
    connect(m_view, &ScoreView::toolClicked, this, &MainWindow::onToolClicked);
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { updateNavigation(); });
    connect(m_editor, &Editor::cursorChanged, this, &MainWindow::updateStatus);
    connect(m_editor, &Editor::message, this, [this](const QString& t) {
        statusBar()->showMessage(t, 4000);
        QApplication::beep();
    });
    connect(m_editor, &Editor::hint, this, [this](const QString& t) { statusBar()->showMessage(t, 5000); });
    connect(m_editor->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);

    m_perf->setText(QStringLiteral("Gravação: %1 ms").arg(m_view->lastLayoutMs(), 0, 'f', 1));
    resize(1280, 880);

    QSettings st;
    restoreGeometry(st.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(st.value(QStringLiteral("window/state")).toByteArray());
    setTool(Tool(st.value(QStringLiteral("window/tool"), int(Tool::SpeedyEntry)).toInt()));

    updateTitle();
    updateStatus();
    updateNavigation();
    m_view->setFocus();
}

// ---------------------------------------------------------------------------------------------
// Menus

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(QStringLiteral("&Arquivo"));
    QMenu* newMenu = file->addMenu(QStringLiteral("&Novo"));
    newMenu->addAction(QStringLiteral("&Assistente de nova partitura…"), QKeySequence::New, this, &MainWindow::newScore);
    newMenu->addAction(QStringLiteral("Partitura em &branco (piano)"), this, [this] {
        if (!maybeSave()) return;
        m_editor->setScore(Score::makeEmpty(QStringLiteral("Sem título"),
                                            { { QStringLiteral("Piano"), Clef::Treble }, { QString(), Clef::Bass } },
                                            TimeSig{ 4, 4 }, 0, 32));
        m_path.clear();
        updateTitle();
    });
    newMenu->addAction(QStringLiteral("Partitura de &demonstração"), this, [this] {
        if (!maybeSave()) return;
        m_editor->setScore(Score::makeDemo());
        m_path.clear();
        updateTitle();
    });
    file->addAction(QStringLiteral("&Abrir…"), QKeySequence::Open, this, &MainWindow::open);
    file->addSeparator();
    file->addAction(QStringLiteral("&Salvar"), QKeySequence::Save, this, &MainWindow::save);
    file->addAction(QStringLiteral("Salvar &como…"), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    file->addSeparator();
    QMenu* imp = file->addMenu(QStringLiteral("&Importar"));
    imp->addAction(QStringLiteral("&MusicXML (.musicxml, .mxl)…"), QKeySequence(QStringLiteral("Ctrl+Shift+I")), this, &MainWindow::importMusicXml);
    disabledAction(imp, QStringLiteral("Arquivo MIDI…"));
    disabledAction(imp, QStringLiteral("Finale (.musx)…"));
    QMenu* exp = file->addMenu(QStringLiteral("&Exportar"));
    exp->addAction(QStringLiteral("&PDF…"), QKeySequence(QStringLiteral("Ctrl+Shift+E")), this, &MainWindow::exportPdf);
    disabledAction(exp, QStringLiteral("MusicXML…"));
    disabledAction(exp, QStringLiteral("Arquivo MIDI…"));
    disabledAction(exp, QStringLiteral("Áudio…"));
    file->addSeparator();
    file->addAction(QStringLiteral("&Imprimir…"), QKeySequence::Print, this, &MainWindow::print);
    file->addSeparator();
    file->addAction(QStringLiteral("Sai&r"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* edit = menuBar()->addMenu(QStringLiteral("&Editar"));
    QAction* undo = m_editor->undoStack()->createUndoAction(this, QStringLiteral("Desfazer"));
    undo->setShortcut(QKeySequence::Undo);
    QAction* redo = m_editor->undoStack()->createRedoAction(this, QStringLiteral("Refazer"));
    redo->setShortcuts({ QKeySequence::Redo, QKeySequence(QStringLiteral("Ctrl+Y")) });
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    disabledAction(edit, QStringLiteral("Recortar"));
    disabledAction(edit, QStringLiteral("Copiar"));
    disabledAction(edit, QStringLiteral("Colar"));
    edit->addSeparator();
    edit->addAction(QStringLiteral("Selecionar &tudo"), QKeySequence::SelectAll, this, [this] {
        if (m_tool != Tool::Selection && m_tool != Tool::Measure) setTool(Tool::Selection);
        m_editor->selectAll();
    });
    edit->addAction(QStringLiteral("&Limpar conteúdo da seleção"), this, [this] { m_editor->clearSelectionContents(); });

    QMenu* util = menuBar()->addMenu(QStringLiteral("&Utilitários"));
    disabledAction(util, QStringLiteral("Transpor…"));
    disabledAction(util, QStringLiteral("Verificar notas fora da extensão…"));
    disabledAction(util, QStringLiteral("Mudar instrumento…"));

    QMenu* view = menuBar()->addMenu(QStringLiteral("E&xibir"));
    m_continuousAction = view->addAction(QStringLiteral("Visualização &contínua"));
    m_continuousAction->setCheckable(true);
    m_continuousAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(m_continuousAction, &QAction::toggled, this, [this](bool on) {
        m_view->setContinuous(on);
        statusBar()->showMessage(on ? QStringLiteral("Visualização contínua") : QStringLiteral("Visualização de página"), 2500);
    });
    view->addSeparator();
    QMenu* zoom = view->addMenu(QStringLiteral("&Zoom"));
    for (int pct : { 50, 75, 100, 150, 200, 400 })
        zoom->addAction(QStringLiteral("%1%").arg(pct), this, [this, pct] { m_view->setZoom(pct / 100.0 * logicalDpiX() / 72.0); });
    view->addAction(QStringLiteral("Aumentar zoom"), QKeySequence::ZoomIn, this, [this] { m_view->setZoom(m_view->zoom() * 1.25); });
    view->addAction(QStringLiteral("Diminuir zoom"), QKeySequence::ZoomOut, this, [this] { m_view->setZoom(m_view->zoom() / 1.25); });

    QMenu* doc = menuBar()->addMenu(QStringLiteral("&Documento"));
    doc->addAction(QStringLiteral("&Título e compositor…"), this, &MainWindow::documentSetup);
    doc->addAction(QStringLiteral("&Armadura de clave…"), this, &MainWindow::chooseKey);
    doc->addAction(QStringLiteral("&Fórmula de compasso…"), this, &MainWindow::chooseTime);
    doc->addSeparator();
    doc->addAction(QStringLiteral("Adicionar &pauta…"), this, &MainWindow::addStaffDialog);
    doc->addAction(QStringLiteral("Adicionar &compassos…"), this, &MainWindow::addMeasuresDialog);

    QMenu* midi = menuBar()->addMenu(QStringLiteral("&MIDI/Áudio"));
    m_playAction = midi->addAction(icons::transport(icons::Transport::Play), QStringLiteral("&Reproduzir / parar"));
    m_playAction->setShortcut(QKeySequence(Qt::Key_Space));
    connect(m_playAction, &QAction::triggered, this, &MainWindow::togglePlay);
    midi->addAction(icons::transport(icons::Transport::Rewind), QStringLiteral("&Voltar ao início"), QKeySequence(QStringLiteral("Ctrl+Home")), this, &MainWindow::rewind);
    midi->addAction(QStringLiteral("&Andamento…"), this, [this] {
        bool ok = false;
        const int v = QInputDialog::getInt(this, QStringLiteral("Andamento"), QStringLiteral("Semínimas por minuto (♩ =):"),
                                           m_editor->score().tempo, 20, 400, 1, &ok);
        if (ok) m_editor->setTempo(v);
    });
    midi->addSeparator();
    midi->addAction(QStringLiteral("Saída de som…"), this, [this] {
        QMessageBox::information(this, QStringLiteral("Saída de som"),
                                 m_player->available()
                                     ? QStringLiteral("O Pauta toca usando: <b>%1</b>.").arg(m_player->outputName())
                                     : QStringLiteral("Nenhuma saída de som disponível: %1").arg(m_player->outputName()));
    });
    disabledAction(midi, QStringLiteral("Configurar dispositivo MIDI…"));
    disabledAction(midi, QStringLiteral("Banco de sons (SoundFont)…"));

    QMenu* plugins = menuBar()->addMenu(QStringLiteral("&Plug-ins"));
    disabledAction(plugins, QStringLiteral("Gerenciar plug-ins…"));

    m_toolMenu = menuBar()->addMenu(QString());

    m_windowMenu = menuBar()->addMenu(QStringLiteral("&Janela"));

    QMenu* help = menuBar()->addMenu(QStringLiteral("A&juda"));
    help->addAction(QStringLiteral("&Atalhos de entrada de notas"), QKeySequence::HelpContents, this, &MainWindow::showShortcuts);
    help->addAction(QStringLiteral("&Sobre o Pauta"), this, [this] {
        QMessageBox::about(this, QStringLiteral("Sobre o Pauta"),
                           QStringLiteral("<b>Pauta</b> %1<br>Editor de partituras leve, inspirado no fluxo de trabalho do Finale.<br><br>"
                                          "Fonte musical Bravura © Steinberg Media Technologies (SIL Open Font License).")
                               .arg(QApplication::applicationVersion()));
    });
}

void MainWindow::togglePlay()
{
    if (m_player->isPlaying()) {
        m_player->stop();
        return;
    }
    if (!m_player->available()) {
        QMessageBox::warning(this, QStringLiteral("Reprodução"),
                             QStringLiteral("Não há saída de som disponível.\n\n%1").arg(m_player->outputName()));
        return;
    }
    // From the start of the selection, or from the measure under the cursor.
    const int from = m_editor->selection().active ? m_editor->selection().measure0 : m_editor->cursor().measure;
    m_player->play(m_editor->score(), from);
    statusBar()->showMessage(QStringLiteral("Reproduzindo a partir do compasso %1 — Espaço para parar").arg(from + 1), 3000);
}

void MainWindow::rewind()
{
    const bool wasPlaying = m_player->isPlaying();
    m_player->stop();
    Cursor c = m_editor->cursor();
    c.measure = 0;
    c.index = 0;
    m_editor->setCursor(c);
    m_editor->clearSelection();
    m_scroll->verticalScrollBar()->setValue(0);
    if (wasPlaying) togglePlay();
}

void MainWindow::comingSoon(const QString& what)
{
    statusBar()->showMessage(QStringLiteral("%1: em breve.").arg(what), 3000);
}

// ---------------------------------------------------------------------------------------------
// Palettes

void MainWindow::buildToolPalette()
{
    m_toolPalette = addToolBar(QStringLiteral("Paleta principal de ferramentas"));
    m_toolPalette->setObjectName(QStringLiteral("toolPalette"));
    m_toolPalette->setIconSize(QSize(26, 26));
    m_toolGroup = new QActionGroup(this);
    for (const ToolInfo& info : toolList()) {
        if (info.groupBreak) m_toolPalette->addSeparator();
        QAction* a = m_toolPalette->addAction(icons::tool(info.tool), info.name);
        a->setCheckable(true);
        a->setData(int(info.tool));
        a->setToolTip(info.available ? QStringLiteral("<b>%1</b><br>%2").arg(info.name, info.hint)
                                     : QStringLiteral("<b>%1</b><br>Em breve.").arg(info.name));
        a->setEnabled(info.available);
        m_toolGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, t = info.tool] { setTool(t); });
    }
    m_windowMenu->addAction(m_toolPalette->toggleViewAction());
}

void MainWindow::buildSimplePalette()
{
    auto* dock = new QDockWidget(QStringLiteral("Entrada simples"), this);
    dock->setObjectName(QStringLiteral("simplePalette"));
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    auto* w = new QWidget;
    auto* grid = new QGridLayout(w);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setSpacing(2);
    int n = 0;
    auto place = [&](QAction* a) {
        auto* b = new QToolButton;
        b->setDefaultAction(a);
        b->setIconSize(QSize(26, 26));
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        grid->addWidget(b, n / 2, n % 2);
        ++n;
    };

    m_durGroup = new QActionGroup(this);
    struct D { int log; const char* key; };
    const D durs[] = { { -1, "8" }, { 0, "7" }, { 1, "6" }, { 2, "5" }, { 3, "4" }, { 4, "3" }, { 5, "2" }, { 6, "1" } };
    for (const D& d : durs) {
        QAction* a = new QAction(icons::glyph(smufl::metronomeNoteFor(d.log)), durationName(d.log), this);
        a->setCheckable(true);
        a->setData(d.log);
        a->setToolTip(QStringLiteral("%1 (tecla %2)").arg(durationName(d.log), QString::fromLatin1(d.key)));
        m_durGroup->addAction(a);
        if (d.log == m_editor->durLog()) a->setChecked(true);
        connect(a, &QAction::triggered, this, [this, d] {
            if (m_tool == Tool::SpeedyEntry && m_editor->currentEvent()) m_editor->enterDuration(d.log);
            else m_editor->setDurLog(d.log);
            m_view->setFocus();
        });
        place(a);
    }

    m_restAction = new QAction(icons::glyph(smufl::RestQuarter), QStringLiteral("Pausa"), this);
    m_restAction->setCheckable(true);
    m_restAction->setToolTip(QStringLiteral("Modo pausa: o clique insere pausa (na entrada rápida, tecla R)"));
    connect(m_restAction, &QAction::toggled, this, [this](bool on) { m_view->setSimpleRestMode(on); });
    place(m_restAction);

    auto tool = [&](char16_t g, const QString& tip, auto fn) {
        QAction* a = new QAction(icons::glyph(g), tip, this);
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, [this, fn] { fn(); m_view->setFocus(); });
        place(a);
    };
    tool(smufl::AugmentationDot, QStringLiteral("Ponto de aumento (.)"), [this] { m_editor->toggleDot(); });
    tool(smufl::AccFlat, QStringLiteral("Bemol (-)"), [this] { m_editor->alter(-1); });
    tool(smufl::AccNatural, QStringLiteral("Bequadro (N)"), [this] { m_editor->naturalize(); });
    tool(smufl::AccSharp, QStringLiteral("Sustenido (+)"), [this] { m_editor->alter(1); });

    {
        QAction* tie = new QAction(icons::tool(Tool::SmartShape), QStringLiteral("Ligadura de valor (= ou L)"), this);
        tie->setToolTip(QStringLiteral("Ligadura de valor com a próxima nota (= ou L)"));
        connect(tie, &QAction::triggered, this, [this] { m_editor->toggleTie(); m_view->setFocus(); });
        place(tie);
    }
    {
        QAction* tup = new QAction(icons::tool(Tool::Tuplet), QStringLiteral("Tercina (Ctrl+3)"), this);
        tup->setToolTip(QStringLiteral("Tercina: a próxima nota começa um grupo de 3 (Ctrl+3; Ctrl+5 quintina, Ctrl+6 sextina…)"));
        connect(tup, &QAction::triggered, this, [this] { m_editor->armTuplet(3); m_view->setFocus(); });
        place(tup);
    }

    grid->setRowStretch(n / 2 + 1, 1);
    w->setMinimumWidth(140);
    dock->setWidget(w);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    m_windowMenu->addAction(dock->toggleViewAction());
}

void MainWindow::buildNavigationBar()
{
    QToolBar* nav = new QToolBar(QStringLiteral("Navegação e reprodução"), this);
    nav->setObjectName(QStringLiteral("navigation"));
    nav->setIconSize(QSize(18, 18));
    addToolBar(Qt::BottomToolBarArea, nav);

    nav->addWidget(new QLabel(QStringLiteral(" Página ")));
    m_pageBox = new QSpinBox;
    m_pageBox->setMinimum(1);
    m_pageBox->setFocusPolicy(Qt::ClickFocus);
    nav->addWidget(m_pageBox);
    connect(m_pageBox, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_updatingNav) return;
        m_scroll->verticalScrollBar()->setValue(int(m_view->pageRect(v - 1).top()) - 12);
    });

    nav->addWidget(new QLabel(QStringLiteral("  Compasso ")));
    m_measureBox = new QSpinBox;
    m_measureBox->setMinimum(1);
    m_measureBox->setFocusPolicy(Qt::ClickFocus);
    nav->addWidget(m_measureBox);
    connect(m_measureBox, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_updatingNav) return;
        Cursor c = m_editor->cursor();
        c.measure = v - 1;
        c.index = 0;
        m_editor->setCursor(c);
        if (const MeasureSlot* s = m_view->layout().slot(c.staff, c.measure)) {
            const QRectF pr = m_view->pageRect(s->page);
            const qreal z = m_view->zoom();
            m_scroll->ensureVisible(int(pr.left() + s->x0 * z), int(pr.top() + s->staffTop * z), 80, 120);
        }
    });

    nav->addSeparator();
    nav->addWidget(new QLabel(QStringLiteral(" Zoom ")));
    m_zoomBox = new QComboBox;
    m_zoomBox->setEditable(true);
    m_zoomBox->setFocusPolicy(Qt::ClickFocus);
    for (int pct : { 50, 75, 100, 125, 150, 200, 300, 400 }) m_zoomBox->addItem(QStringLiteral("%1%").arg(pct), pct);
    nav->addWidget(m_zoomBox);
    connect(m_zoomBox, &QComboBox::textActivated, this, [this](const QString& t) {
        QString s = t;
        s.remove(QLatin1Char('%'));
        bool ok = false;
        const double pct = s.trimmed().toDouble(&ok);
        if (ok && pct > 0) m_view->setZoom(pct / 100.0 * logicalDpiX() / 72.0);
        m_view->setFocus();
    });

    // Layer selector (Finale: layers 1-4, each with its own colour).
    nav->addSeparator();
    nav->addWidget(new QLabel(QStringLiteral(" Camada ")));
    m_layerGroup = new QActionGroup(this);
    static const char* layerColors[4] = { "#1e6fd9", "#d62828", "#1e963c", "#2846c8" };
    for (int v = 0; v < kVoices; ++v) {
        QAction* a = new QAction(QString::number(v + 1), this);
        a->setCheckable(true);
        a->setData(v);
        a->setToolTip(QStringLiteral("Camada %1 (Alt+%1)%2").arg(v + 1).arg(v % 2 == 0 ? QStringLiteral(" — hastes para cima quando há outra camada")
                                                                                          : QStringLiteral(" — hastes para baixo quando há outra camada")));
        m_layerGroup->addAction(a);
        nav->addAction(a);
        if (auto* b = qobject_cast<QToolButton*>(nav->widgetForAction(a))) {
            b->setStyleSheet(QStringLiteral("QToolButton { color: %1; font-weight: bold; min-width: 18px; }").arg(QLatin1String(layerColors[v])));
            b->setFocusPolicy(Qt::NoFocus);
        }
        if (v == 0) a->setChecked(true);
        connect(a, &QAction::triggered, this, [this, v] { m_editor->setVoice(v); m_view->setFocus(); });
    }

    nav->addSeparator();
    QAction* rew = nav->addAction(icons::transport(icons::Transport::Rewind), QStringLiteral("Voltar ao início"));
    rew->setToolTip(QStringLiteral("Voltar ao início (Ctrl+Home)"));
    connect(rew, &QAction::triggered, this, &MainWindow::rewind);
    nav->addAction(m_playAction);
    m_playAction->setToolTip(QStringLiteral("Reproduzir a partir do compasso atual (Espaço)"));
    QAction* stopA = nav->addAction(icons::transport(icons::Transport::Stop), QStringLiteral("Parar"));
    stopA->setToolTip(QStringLiteral("Parar (Espaço)"));
    connect(stopA, &QAction::triggered, this, [this] { m_player->stop(); });
    QAction* rec = nav->addAction(icons::transport(icons::Transport::Record), QStringLiteral("Gravar (teclado MIDI)"));
    rec->setToolTip(QStringLiteral("Gravar com teclado MIDI — em breve"));
    rec->setEnabled(false);

    nav->addWidget(new QLabel(QStringLiteral("  ♩ = ")));
    m_tempoBox = new QSpinBox;
    m_tempoBox->setRange(20, 400);
    m_tempoBox->setValue(m_editor->score().tempo);
    m_tempoBox->setFocusPolicy(Qt::ClickFocus);
    m_tempoBox->setToolTip(QStringLiteral("Andamento (semínimas por minuto)"));
    nav->addWidget(m_tempoBox);
    connect(m_tempoBox, &QSpinBox::editingFinished, this, [this] {
        m_editor->setTempo(m_tempoBox->value());
        m_view->setFocus();
    });
    connect(m_editor, &Editor::scoreChanged, this, [this] {
        if (!m_tempoBox->hasFocus()) m_tempoBox->setValue(m_editor->score().tempo);
    });

    connect(m_player, &playback::Player::measureChanged, this, [this](int m) { m_view->setPlaybackMeasure(m); });
    connect(m_player, &playback::Player::stateChanged, this, [this](bool playing) {
        m_playAction->setIcon(icons::transport(playing ? icons::Transport::Stop : icons::Transport::Play));
        m_playAction->setText(playing ? QStringLiteral("Parar") : QStringLiteral("Reproduzir"));
    });
    m_windowMenu->addAction(nav->toggleViewAction());
}

// ---------------------------------------------------------------------------------------------
// Tools

void MainWindow::setTool(Tool t)
{
    if (!toolInfo(t).available) t = Tool::SpeedyEntry;
    m_tool = t;
    m_view->setTool(t);
    for (QAction* a : m_toolGroup->actions())
        if (a->data().toInt() == int(t)) a->setChecked(true);
    rebuildToolMenu();
    m_status->setToolTip(toolInfo(t).hint);
    updateStatus();
    QSettings().setValue(QStringLiteral("window/tool"), int(t));
    m_view->setFocus();
}

void MainWindow::rebuildToolMenu()
{
    m_toolMenu->clear();
    const ToolInfo& info = toolInfo(m_tool);
    m_toolMenu->setTitle(info.name);
    Editor* ed = m_editor;

    switch (m_tool) {
    case Tool::Selection:
        m_toolMenu->addAction(QStringLiteral("Selecionar tudo"), this, [ed] { ed->selectAll(); });
        m_toolMenu->addAction(QStringLiteral("Limpar seleção"), this, [ed] { ed->clearSelection(); });
        m_toolMenu->addAction(QStringLiteral("Limpar conteúdo (Delete)"), this, [ed] { ed->clearSelectionContents(); });
        break;
    case Tool::Zoom:
        m_toolMenu->addAction(QStringLiteral("Aumentar"), this, [this] { m_view->setZoom(m_view->zoom() * 1.25); });
        m_toolMenu->addAction(QStringLiteral("Diminuir"), this, [this] { m_view->setZoom(m_view->zoom() / 1.25); });
        m_toolMenu->addAction(QStringLiteral("100%"), this, [this] { m_view->setZoom(logicalDpiX() / 72.0); });
        break;
    case Tool::Hand:
        m_toolMenu->addAction(QStringLiteral("Arraste a página para movê-la"))->setEnabled(false);
        break;
    case Tool::Staff:
        m_toolMenu->addAction(QStringLiteral("Adicionar pauta…"), this, &MainWindow::addStaffDialog);
        m_toolMenu->addAction(QStringLiteral("Editar pauta atual…"), this, [this] { editStaff(m_editor->cursor().staff); });
        m_toolMenu->addAction(QStringLiteral("Excluir pauta atual"), this, [ed] { ed->removeStaff(ed->cursor().staff); });
        break;
    case Tool::Clef:
        for (Clef c : { Clef::Treble, Clef::Bass, Clef::Alto, Clef::Tenor })
            m_toolMenu->addAction(clefName(c), this, [ed, c] { ed->setClef(ed->cursor().staff, c); });
        break;
    case Tool::KeySig:
        m_toolMenu->addAction(QStringLiteral("Mudar armadura…"), this, &MainWindow::chooseKey);
        break;
    case Tool::TimeSig:
        m_toolMenu->addAction(QStringLiteral("Mudar fórmula de compasso…"), this, &MainWindow::chooseTime);
        break;
    case Tool::Measure:
        m_toolMenu->addAction(QStringLiteral("Adicionar compassos ao final…"), this, &MainWindow::addMeasuresDialog);
        m_toolMenu->addAction(QStringLiteral("Inserir compassos antes da seleção…"), this, [this] {
            bool ok = false;
            const int n = QInputDialog::getInt(this, QStringLiteral("Inserir compassos"), QStringLiteral("Quantos compassos?"), 1, 1, 500, 1, &ok);
            if (ok) m_editor->insertMeasuresBefore(n);
        });
        m_toolMenu->addAction(QStringLiteral("Excluir compassos selecionados"), this, [ed] { ed->deleteSelectedMeasures(); });
        break;
    case Tool::SimpleEntry:
        for (QAction* a : m_durGroup->actions()) m_toolMenu->addAction(a);
        m_toolMenu->addSeparator();
        m_toolMenu->addAction(m_restAction);
        break;
    case Tool::SpeedyEntry:
        m_toolMenu->addAction(QStringLiteral("Atalhos de teclado…"), this, &MainWindow::showShortcuts);
        m_toolMenu->addSeparator();
        for (QAction* a : m_layerGroup->actions()) m_toolMenu->addAction(a);
        disabledAction(m_toolMenu, QStringLiteral("Usar teclado MIDI para a altura"));
        break;
    default:
        disabledAction(m_toolMenu, info.name);
        break;
    }
}

void MainWindow::onToolClicked(Tool tool, int staff, int measure)
{
    Cursor c = m_editor->cursor();
    c.staff = staff;
    c.measure = measure;
    c.index = 0;
    m_editor->setCursor(c);
    switch (tool) {
    case Tool::Staff: editStaff(staff); break;
    case Tool::Clef: {
        QStringList names;
        for (Clef cl : { Clef::Treble, Clef::Bass, Clef::Alto, Clef::Tenor }) names << clefName(cl);
        bool ok = false;
        const QString pick = QInputDialog::getItem(this, QStringLiteral("Clave"), QStringLiteral("Clave desta pauta:"), names,
                                                   int(m_editor->score().staves[size_t(staff)].clef), false, &ok);
        if (ok) m_editor->setClef(staff, Clef(names.indexOf(pick)));
        break;
    }
    case Tool::KeySig: chooseKey(); break;
    case Tool::TimeSig: chooseTime(); break;
    default: break;
    }
}

void MainWindow::chooseKey()
{
    QStringList names;
    for (int f = -7; f <= 7; ++f) names << keyName(f);
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, QStringLiteral("Armadura de clave"), QStringLiteral("Tonalidade:"), names,
                                               m_editor->score().keyFifths + 7, false, &ok);
    if (ok) m_editor->setKeySignature(names.indexOf(pick) - 7);
}

void MainWindow::chooseTime()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Fórmula de compasso"));
    auto* form = new QFormLayout(&dlg);
    auto* beats = new QSpinBox;
    beats->setRange(1, 32);
    beats->setValue(m_editor->score().time.beats);
    auto* type = new QComboBox;
    for (int v : { 1, 2, 4, 8, 16, 32 }) type->addItem(QString::number(v), v);
    type->setCurrentIndex(type->findData(m_editor->score().time.beatType));
    form->addRow(QStringLiteral("Tempos por compasso:"), beats);
    form->addRow(QStringLiteral("Unidade de tempo:"), type);
    form->addRow(new QLabel(QStringLiteral("<small>As notas são redistribuídas nos novos compassos.</small>")));
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() == QDialog::Accepted) m_editor->setTimeSignature(TimeSig{ beats->value(), type->currentData().toInt() });
}

void MainWindow::editStaff(int staff)
{
    const Staff& st = m_editor->score().staves[size_t(staff)];
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Pauta %1").arg(staff + 1));
    auto* form = new QFormLayout(&dlg);
    auto* name = new QLineEdit(st.name);
    auto* clef = new QComboBox;
    for (Clef c : { Clef::Treble, Clef::Bass, Clef::Alto, Clef::Tenor }) clef->addItem(clefName(c));
    clef->setCurrentIndex(int(st.clef));
    form->addRow(QStringLiteral("Nome:"), name);
    form->addRow(QStringLiteral("Clave:"), clef);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    if (name->text() != st.name) m_editor->setStaffName(staff, name->text());
    if (Clef(clef->currentIndex()) != m_editor->score().staves[size_t(staff)].clef) m_editor->setClef(staff, Clef(clef->currentIndex()));
}

void MainWindow::addStaffDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Adicionar pauta"));
    auto* form = new QFormLayout(&dlg);
    auto* name = new QLineEdit;
    auto* clef = new QComboBox;
    for (Clef c : { Clef::Treble, Clef::Bass, Clef::Alto, Clef::Tenor }) clef->addItem(clefName(c));
    auto* where = new QComboBox;
    where->addItem(QStringLiteral("Abaixo da pauta atual"));
    where->addItem(QStringLiteral("Acima da pauta atual"));
    where->addItem(QStringLiteral("No fim da grade"));
    form->addRow(QStringLiteral("Nome:"), name);
    form->addRow(QStringLiteral("Clave:"), clef);
    form->addRow(QStringLiteral("Posição:"), where);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    const int cur = m_editor->cursor().staff;
    const int idx = where->currentIndex() == 0 ? cur + 1 : where->currentIndex() == 1 ? cur : int(m_editor->score().staves.size());
    m_editor->addStaff(idx, name->text(), Clef(clef->currentIndex()));
}

void MainWindow::addMeasuresDialog()
{
    bool ok = false;
    const int n = QInputDialog::getInt(this, QStringLiteral("Adicionar compassos"),
                                       QStringLiteral("Quantos compassos adicionar ao final?"), 4, 1, 1000, 1, &ok);
    if (ok) m_editor->addMeasures(n);
}

void MainWindow::documentSetup()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Título e compositor"));
    auto* form = new QFormLayout(&dlg);
    auto* title = new QLineEdit(m_editor->score().title);
    auto* composer = new QLineEdit(m_editor->score().composer);
    title->setMinimumWidth(280);
    form->addRow(QStringLiteral("Título:"), title);
    form->addRow(QStringLiteral("Compositor:"), composer);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() == QDialog::Accepted) m_editor->setTitleComposer(title->text(), composer->text());
}

// ---------------------------------------------------------------------------------------------
// Status, navigation

void MainWindow::updateStatus()
{
    const Cursor& c = m_editor->cursor();
    const Score& s = m_editor->score();
    if (s.staves.empty()) return;
    const Staff& st = s.staves[size_t(c.staff)];
    const QString staffName = st.name.isEmpty() ? QStringLiteral("Pauta %1").arg(c.staff + 1) : st.name;
    const Pitch p = Pitch::fromDiatonic(c.diatonic, keyAlterFor(((c.diatonic % 7) + 7) % 7, s.keyFifths));
    m_status->setText(QStringLiteral("%1  ·  %2  ·  Camada %3  ·  Compasso %4 de %5  ·  Cursor: %6  ·  Duração: %7")
                          .arg(toolInfo(m_tool).name, staffName)
                          .arg(c.voice + 1)
                          .arg(c.measure + 1)
                          .arg(s.measureCount())
                          .arg(pitchName(p), durationName(m_editor->durLog())));
    if (m_durGroup)
        for (QAction* a : m_durGroup->actions())
            if (a->data().toInt() == m_editor->durLog()) a->setChecked(true);
    if (m_layerGroup)
        for (QAction* a : m_layerGroup->actions())
            if (a->data().toInt() == c.voice) a->setChecked(true);
    updateNavigation();
}

void MainWindow::updateNavigation()
{
    if (!m_pageBox) return;
    m_updatingNav = true;
    const int pages = std::max<int>(1, int(m_view->layout().pages.size()));
    m_pageBox->setMaximum(pages);
    m_pageBox->setSuffix(QStringLiteral(" de %1").arg(pages));
    const int y = m_scroll->verticalScrollBar()->value() + m_scroll->viewport()->height() / 3;
    m_pageBox->setValue(m_view->pageAt(y) + 1);
    m_measureBox->setMaximum(std::max(1, m_editor->score().measureCount()));
    m_measureBox->setValue(m_editor->cursor().measure + 1);
    const int pct = int(std::lround(m_view->zoom() * 72.0 / logicalDpiX() * 100));
    m_zoomBox->setEditText(QStringLiteral("%1%").arg(pct));
    m_updatingNav = false;
}

void MainWindow::updateTitle()
{
    const QString name = m_path.isEmpty() ? QStringLiteral("Sem título") : QFileInfo(m_path).fileName();
    setWindowTitle(QStringLiteral("%1[*] — Pauta").arg(name));
    setWindowModified(!m_editor->undoStack()->isClean());
}

// ---------------------------------------------------------------------------------------------
// Files

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
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Abrir partitura"), QString(), io::openFileFilter());
    if (!path.isEmpty()) openFile(path);
}

void MainWindow::importMusicXml()
{
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Importar MusicXML"), QString(),
                                                      QStringLiteral("MusicXML (*.musicxml *.mxl *.xml)"));
    if (!path.isEmpty()) openFile(path);
}

bool MainWindow::openFile(const QString& path)
{
    Score s;
    QString err;
    QStringList warnings;
    if (!io::loadAnyScore(s, path, &err, &warnings)) {
        QMessageBox::warning(this, QStringLiteral("Erro ao abrir"),
                             QStringLiteral("Não foi possível abrir %1.\n\n%2").arg(QFileInfo(path).fileName(), err));
        return false;
    }
    m_editor->setScore(std::move(s));
    // Imported files are saved as a new .pauta (the original MusicXML is never overwritten).
    m_path = io::isMusicXmlPath(path) ? QString() : path;
    updateTitle();
    if (io::isMusicXmlPath(path)) {
        setWindowTitle(QStringLiteral("%1 (importado)[*] — Pauta").arg(QFileInfo(path).fileName()));
        statusBar()->showMessage(QStringLiteral("MusicXML importado: %1").arg(QFileInfo(path).fileName()), 5000);
    }
    if (!warnings.isEmpty())
        QMessageBox::information(this, QStringLiteral("Importação concluída com observações"),
                                 QStringLiteral("A partitura foi importada. Algumas coisas foram simplificadas:\n\n• ")
                                     + warnings.join(QStringLiteral("\n• ")));
    return true;
}

void MainWindow::exportPdf()
{
    const QString base = m_path.isEmpty() ? QStringLiteral("partitura.pdf") : QFileInfo(m_path).completeBaseName() + QStringLiteral(".pdf");
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Exportar PDF"), base, QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    // Always export the page view, even when the scroll view is on screen.
    const Layout layout = m_view->continuous() ? engrave(m_editor->score()) : m_view->layout();
    if (!pauta::exportPdf(layout, path, m_editor->score().title))
        QMessageBox::warning(this, QStringLiteral("Erro"), QStringLiteral("Não foi possível criar o PDF."));
    else
        statusBar()->showMessage(QStringLiteral("PDF exportado: %1").arg(path), 4000);
}

void MainWindow::print()
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setDocName(m_editor->score().title);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const Layout layout = engrave(m_editor->score());
    QPainter p(&printer);
    const QRectF page = printer.pageRect(QPrinter::DevicePixel);
    const qreal k = std::min(page.width() / layout.pageWidth, page.height() / layout.pageHeight);
    int from = printer.fromPage() > 0 ? printer.fromPage() - 1 : 0;
    int to = printer.toPage() > 0 ? printer.toPage() - 1 : int(layout.pages.size()) - 1;
    to = std::min(to, int(layout.pages.size()) - 1);
    for (int i = from; i <= to; ++i) {
        if (i > from) printer.newPage();
        p.save();
        p.scale(k, k);
        paintPage(p, layout, i);
        p.restore();
    }
}

void MainWindow::runLaunchWindow()
{
    if (!LaunchDialog::showAtStartup()) return;
    LaunchDialog dlg(this);
    dlg.exec();
    switch (dlg.choice()) {
    case LaunchDialog::Wizard: newScore(); break;
    case LaunchDialog::Blank:
        m_editor->setScore(Score::makeEmpty(QStringLiteral("Sem título"),
                                            { { QStringLiteral("Piano"), Clef::Treble }, { QString(), Clef::Bass } },
                                            TimeSig{ 4, 4 }, 0, 32));
        break;
    case LaunchDialog::Open: open(); break;
    case LaunchDialog::Demo:
    case LaunchDialog::None: break;
    }
    updateTitle();
}

void MainWindow::showShortcuts()
{
    QMessageBox::information(this, QStringLiteral("Entrada rápida — atalhos"), QStringLiteral(
        "<table cellpadding=3>"
        "<tr><td><b>↑ / ↓</b></td><td>Move o cursor de altura (Ctrl: oitava)</td></tr>"
        "<tr><td><b>← / →</b></td><td>Nota anterior / próxima (Ctrl: compasso)</td></tr>"
        "<tr><td><b>Alt + ↑ / ↓</b></td><td>Pauta de cima / de baixo</td></tr>"
        "<tr><td><b>1 – 8</b></td><td>Insere nota na altura do cursor: 7 semibreve, 6 mínima, 5 semínima, "
        "4 colcheia, 3 semicolcheia, 2 fusa, 1 semifusa, 8 breve. Sobre uma nota existente, muda a duração.</td></tr>"
        "<tr><td><b>A – G</b></td><td>Insere a nota pelo nome (C=Dó … B=Si), na oitava mais próxima</td></tr>"
        "<tr><td><b>Enter</b></td><td>Insere nota / adiciona ou remove a nota do cursor no acorde</td></tr>"
        "<tr><td><b>R</b></td><td>Insere pausa / alterna nota ↔ pausa</td></tr>"
        "<tr><td><b>+ / − / N</b></td><td>Sustenido / bemol / bequadro</td></tr>"
        "<tr><td><b>= ou L</b></td><td>Ligadura de valor com a próxima nota</td></tr>"
        "<tr><td><b>.</b></td><td>Ponto de aumento</td></tr>"
        "<tr><td><b>Shift + ↑ / ↓</b></td><td>Transpõe a nota (Ctrl: oitava)</td></tr>"
        "<tr><td><b>Delete / Backspace</b></td><td>Apaga a nota atual / anterior</td></tr>"
        "<tr><td><b>Ctrl + 3</b></td><td>Tercina: a próxima nota começa um grupo de 3 e as seguintes o completam "
        "(Ctrl+2 duína, Ctrl+5 quintina, Ctrl+6 sextina, Ctrl+7 setina)</td></tr>"
        "<tr><td><b>Alt + 1 – 4</b></td><td>Escolhe a camada (voz) 1 a 4. Com duas camadas, a 1 fica com hastes para cima e a 2 para baixo</td></tr>"
        "<tr><td><b>Espaço</b></td><td>Reproduz a partir do compasso atual / para</td></tr>"
        "<tr><td><b>Ctrl + Home</b></td><td>Volta ao início</td></tr>"
        "<tr><td><b>Ctrl + E</b></td><td>Alterna visualização de página / contínua</td></tr>"
        "<tr><td><b>Ctrl + roda do mouse</b></td><td>Zoom</td></tr>"
        "</table>"));
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    QSettings st;
    st.setValue(QStringLiteral("window/geometry"), saveGeometry());
    st.setValue(QStringLiteral("window/state"), saveState());
    e->accept();
}

} // namespace pauta
