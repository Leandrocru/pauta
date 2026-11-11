#pragma once
#include <QMainWindow>

class QLabel;
class QScrollArea;
class QActionGroup;

namespace pauta {

class Editor;
class ScoreView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    bool openFile(const QString& path);
    Editor* editor() const { return m_editor; }
    ScoreView* view() const { return m_view; }

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildMenus();
    void buildPalette();
    void updateStatus();
    void updateTitle();
    bool maybeSave();
    bool save();
    bool saveAs();
    void newScore();
    void open();
    void exportPdf();
    void showShortcuts();

    Editor* m_editor;
    ScoreView* m_view;
    QScrollArea* m_scroll;
    QLabel* m_status;
    QLabel* m_perf;
    QActionGroup* m_durGroup = nullptr;
    QString m_path;
};

} // namespace pauta
