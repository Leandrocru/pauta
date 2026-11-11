#include "ui/LaunchDialog.h"

#include "ui/Icons.h"

#include <QCheckBox>
#include <QLabel>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace pauta {

bool LaunchDialog::showAtStartup()
{
    return QSettings().value(QStringLiteral("launch/show"), true).toBool();
}

LaunchDialog::LaunchDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Pauta"));
    auto* lay = new QVBoxLayout(this);
    auto* title = new QLabel(QStringLiteral("<span style='font-size:20px'><b>Pauta</b></span><br>O que você quer fazer?"));
    lay->addWidget(title);

    auto add = [&](const QIcon& icon, const QString& text, Choice c) {
        auto* b = new QToolButton;
        b->setIcon(icon);
        b->setIconSize(QSize(32, 32));
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setMinimumHeight(48);
        connect(b, &QToolButton::clicked, this, [this, c] { m_choice = c; accept(); });
        lay->addWidget(b);
    };
    add(icons::tool(Tool::SimpleEntry), QStringLiteral("Assistente de nova partitura…"), Wizard);
    add(icons::tool(Tool::Staff), QStringLiteral("Partitura em branco (piano)"), Blank);
    add(icons::tool(Tool::Selection), QStringLiteral("Abrir partitura…"), Open);
    add(icons::tool(Tool::Clef), QStringLiteral("Partitura de demonstração"), Demo);

    auto* again = new QCheckBox(QStringLiteral("Mostrar esta janela ao iniciar"));
    again->setChecked(showAtStartup());
    connect(again, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue(QStringLiteral("launch/show"), on); });
    lay->addWidget(again);
    setMinimumWidth(380);
}

} // namespace pauta
