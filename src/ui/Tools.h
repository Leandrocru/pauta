#pragma once
// Tool modes of the main tool palette. As in Finale's workflow, the active tool decides
// what a click on the score does and which tool menu appears in the menu bar.
#include <QString>
#include <vector>

namespace pauta {

enum class Tool {
    Selection,
    Zoom,
    Hand,
    Staff,
    Clef,
    KeySig,
    TimeSig,
    Measure,
    SimpleEntry,
    SpeedyEntry,
    HyperScribe,
    Tuplet,
    SmartShape,
    Articulation,
    Expression,
    Repeat,
    Chord,
    Lyrics,
    Text,
    PageLayout,
};

struct ToolInfo {
    Tool tool;
    QString name;       // shown in tooltips and as the tool menu title
    QString hint;       // one-line status bar help
    bool available;     // false = shown but disabled ("em breve")
    bool groupBreak;    // separator before this tool in the palette
};

inline const std::vector<ToolInfo>& toolList()
{
    static const std::vector<ToolInfo> list = {
        { Tool::Selection, QStringLiteral("Seleção"), QStringLiteral("Clique ou arraste para selecionar compassos. Delete limpa o conteúdo."), true, false },
        { Tool::Zoom, QStringLiteral("Zoom"), QStringLiteral("Clique para aproximar; clique com o botão direito (ou Ctrl+clique) para afastar."), true, false },
        { Tool::Hand, QStringLiteral("Mão"), QStringLiteral("Arraste para mover a página."), true, false },
        { Tool::Staff, QStringLiteral("Pauta"), QStringLiteral("Clique numa pauta para editar nome e clave. Use o menu Pauta para adicionar ou excluir."), true, true },
        { Tool::Clef, QStringLiteral("Clave"), QStringLiteral("Clique numa pauta para trocar a clave."), true, false },
        { Tool::KeySig, QStringLiteral("Armadura de clave"), QStringLiteral("Clique na partitura para mudar a armadura."), true, false },
        { Tool::TimeSig, QStringLiteral("Fórmula de compasso"), QStringLiteral("Clique na partitura para mudar a fórmula de compasso."), true, false },
        { Tool::Measure, QStringLiteral("Compasso"), QStringLiteral("Clique ou arraste para selecionar compassos. Use o menu Compasso para inserir ou excluir."), true, false },
        { Tool::SimpleEntry, QStringLiteral("Entrada simples"), QStringLiteral("Escolha a duração na paleta e clique na pauta para inserir a nota."), true, true },
        { Tool::SpeedyEntry, QStringLiteral("Entrada rápida"), QStringLiteral("Clique num compasso e digite: números = duração, letras = notas, Enter = acorde. F1 mostra os atalhos."), true, false },
        { Tool::HyperScribe, QStringLiteral("Teclado MIDI"), QStringLiteral("Toque no teclado MIDI: cada nota ou acorde entra no cursor com a duração escolhida (números 1–8). Setas movem o cursor, R = pausa."), true, false },
        { Tool::Tuplet, QStringLiteral("Quiáltera"), QStringLiteral("Em breve."), false, true },
        { Tool::SmartShape, QStringLiteral("Linhas e ligaduras"), QStringLiteral("Escolha ligadura, crescendo ou diminuendo na paleta; clique na primeira nota e depois na última."), true, false },
        { Tool::Articulation, QStringLiteral("Articulação"), QStringLiteral("Escolha a articulação na paleta e clique nas notas (clicar de novo remove)."), true, false },
        { Tool::Expression, QStringLiteral("Expressão"), QStringLiteral("Escolha a dinâmica na paleta e clique na nota onde ela começa."), true, false },
        { Tool::Repeat, QStringLiteral("Repetição"), QStringLiteral("Clique num compasso para editar ritornelos, casas, D.C./D.S. e Coda, ou escolha um sinal no menu Repetição e clique nos compassos."), true, false },
        { Tool::Chord, QStringLiteral("Cifra"), QStringLiteral("Clique numa nota e digite a cifra (C7, Am, F#m7b5, G/B). Espaço passa para a próxima nota; Esc termina."), true, true },
        { Tool::Lyrics, QStringLiteral("Letra"), QStringLiteral("Clique numa nota e digite: Espaço = próxima nota, - = hífen, _ = melisma, ↑/↓ = estrofe, Esc termina."), true, false },
        { Tool::Text, QStringLiteral("Texto"), QStringLiteral("Em breve."), false, false },
        { Tool::PageLayout, QStringLiteral("Layout da página"), QStringLiteral("Em breve."), false, true },
    };
    return list;
}

inline const ToolInfo& toolInfo(Tool t)
{
    for (const ToolInfo& i : toolList())
        if (i.tool == t) return i;
    return toolList().front();
}

} // namespace pauta
