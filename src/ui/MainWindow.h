#pragma once
#include "ui/Tools.h"

#include <QMainWindow>

class QActionGroup;
class QComboBox;
class QLabel;
class QMenu;
class QScrollArea;
class QSpinBox;
class QToolBar;

namespace pauta {

class Editor;
class ScoreView;
namespace playback { class Player; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    bool openFile(const QString& path);
    Editor* editor() const { return m_editor; }
    ScoreView* view() const { return m_view; }

    void setTool(Tool t);
    void runLaunchWindow(); // start-up chooser (wizard / open / demo)

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildMenus();
    void buildToolPalette();
    void buildSimplePalette();
    void buildNavigationBar();
    void rebuildToolMenu();
    void updateStatus();
    void updateTitle();
    void updateNavigation();
    bool maybeSave();
    bool save();
    bool saveAs();
    void newScore();
    void open();
    void importMusicXml();
    void exportPdf();
    void print();
    void documentSetup();
    void showShortcuts();
    void onToolClicked(Tool tool, int staff, int measure);
    void chooseKey();
    void chooseTime();
    void editStaff(int staff);
    void addStaffDialog();
    void addMeasuresDialog();
    void comingSoon(const QString& what);
    void togglePlay();
    void rewind();

    Editor* m_editor;
    ScoreView* m_view;
    QScrollArea* m_scroll;
    QLabel* m_status;
    QLabel* m_perf;
    QToolBar* m_toolPalette = nullptr;
    QActionGroup* m_toolGroup = nullptr;
    QActionGroup* m_durGroup = nullptr;
    QAction* m_restAction = nullptr;
    QMenu* m_toolMenu = nullptr;
    QMenu* m_windowMenu = nullptr;
    QAction* m_continuousAction = nullptr;
    QSpinBox* m_pageBox = nullptr;
    QSpinBox* m_measureBox = nullptr;
    QComboBox* m_zoomBox = nullptr;
    QActionGroup* m_layerGroup = nullptr;
    playback::Player* m_player = nullptr;
    QAction* m_playAction = nullptr;
    QSpinBox* m_tempoBox = nullptr;
    Tool m_tool = Tool::SpeedyEntry;
    QString m_path;
    bool m_updatingNav = false;
};

} // namespace pauta
