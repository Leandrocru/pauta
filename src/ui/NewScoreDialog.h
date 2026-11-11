#pragma once
// "Document Setup Wizard"-style dialog for creating a new score.
#include "model/Score.h"

#include <QDialog>

class QLineEdit;
class QComboBox;
class QSpinBox;

namespace pauta {

class NewScoreDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewScoreDialog(QWidget* parent = nullptr);
    Score result() const;

private:
    QLineEdit* m_title;
    QLineEdit* m_composer;
    QComboBox* m_ensemble;
    QSpinBox* m_beats;
    QComboBox* m_beatType;
    QComboBox* m_key;
    QSpinBox* m_measures;
};

} // namespace pauta
