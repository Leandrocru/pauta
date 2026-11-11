#pragma once
// Start-up window offering the usual ways to begin (new score wizard, open, demo).
#include <QDialog>

namespace pauta {

class LaunchDialog : public QDialog {
    Q_OBJECT
public:
    enum Choice { None, Wizard, Blank, Open, Demo };
    explicit LaunchDialog(QWidget* parent = nullptr);
    Choice choice() const { return m_choice; }

    static bool showAtStartup();

private:
    Choice m_choice = None;
};

} // namespace pauta
