#include "ui/NewScoreDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>

namespace pauta {

namespace {

struct Ensemble {
    const char* name;
    std::vector<std::pair<QString, Clef>> staves;
};

const std::vector<Ensemble>& ensembles()
{
    static const std::vector<Ensemble> list = {
        { "Piano", { { "Piano", Clef::Treble }, { "", Clef::Bass } } },
        { "Voz", { { "Voz", Clef::Treble } } },
        { "Voz e piano", { { "Voz", Clef::Treble }, { "Piano", Clef::Treble }, { "", Clef::Bass } } },
        { "Coro SATB", { { "Soprano", Clef::Treble }, { "Contralto", Clef::Treble }, { "Tenor", Clef::Treble }, { "Baixo", Clef::Bass } } },
        { "Quarteto de cordas", { { "Violino I", Clef::Treble }, { "Violino II", Clef::Treble }, { "Viola", Clef::Alto }, { "Violoncelo", Clef::Bass } } },
        { "Violino", { { "Violino", Clef::Treble } } },
        { "Viola", { { "Viola", Clef::Alto } } },
        { "Violoncelo", { { "Violoncelo", Clef::Bass } } },
        { "Flauta", { { "Flauta", Clef::Treble } } },
        { "Violão", { { "Violão", Clef::Treble } } },
        { "Pauta única (Clave de Sol)", { { "", Clef::Treble } } },
        { "Pauta única (Clave de Fá)", { { "", Clef::Bass } } },
    };
    return list;
}

} // namespace

NewScoreDialog::NewScoreDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Nova partitura"));
    auto* form = new QFormLayout(this);

    m_title = new QLineEdit(QStringLiteral("Sem título"));
    m_composer = new QLineEdit;
    m_ensemble = new QComboBox;
    for (const Ensemble& e : ensembles()) m_ensemble->addItem(QString::fromUtf8(e.name));

    m_beats = new QSpinBox;
    m_beats->setRange(1, 32);
    m_beats->setValue(4);
    m_beatType = new QComboBox;
    for (int v : { 1, 2, 4, 8, 16, 32 }) m_beatType->addItem(QString::number(v), v);
    m_beatType->setCurrentIndex(2);

    m_key = new QComboBox;
    static const char* majors[15] = { "Dó♭", "Sol♭", "Ré♭", "Lá♭", "Mi♭", "Si♭", "Fá", "Dó", "Sol", "Ré", "Lá", "Mi", "Si", "Fá♯", "Dó♯" };
    static const char* minors[15] = { "lá♭", "mi♭", "si♭", "fá", "dó", "sol", "ré", "lá", "mi", "si", "fá♯", "dó♯", "sol♯", "ré♯", "lá♯" };
    for (int f = -7; f <= 7; ++f) {
        const QString acc = f == 0 ? QStringLiteral("sem acidentes")
                                   : QStringLiteral("%1 %2").arg(std::abs(f)).arg(f > 0 ? "♯" : "♭");
        m_key->addItem(QStringLiteral("%1 maior / %2 menor (%3)")
                           .arg(QString::fromUtf8(majors[f + 7]), QString::fromUtf8(minors[f + 7]), acc),
                       f);
    }
    m_key->setCurrentIndex(7);

    m_measures = new QSpinBox;
    m_measures->setRange(1, 2000);
    m_measures->setValue(32);

    form->addRow(QStringLiteral("Título:"), m_title);
    form->addRow(QStringLiteral("Compositor:"), m_composer);
    form->addRow(QStringLiteral("Instrumentação:"), m_ensemble);
    auto* timeRow = new QHBoxLayout;
    timeRow->addWidget(m_beats);
    timeRow->addWidget(m_beatType);
    form->addRow(QStringLiteral("Fórmula de compasso:"), timeRow);
    form->addRow(QStringLiteral("Tonalidade:"), m_key);
    form->addRow(QStringLiteral("Compassos:"), m_measures);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

Score NewScoreDialog::result() const
{
    const Ensemble& e = ensembles()[size_t(std::max(0, m_ensemble->currentIndex()))];
    Score s = Score::makeEmpty(m_title->text(), e.staves,
                               TimeSig{ m_beats->value(), m_beatType->currentData().toInt() },
                               m_key->currentData().toInt(), m_measures->value());
    s.composer = m_composer->text();
    return s;
}

} // namespace pauta
