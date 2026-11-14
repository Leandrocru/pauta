# Pauta

Editor de partituras leve para Windows, inspirado no fluxo de trabalho do Finale
(descontinuado em 2024). Feito para rodar bem em computadores modestos e abrir
o maior número possível de formatos de partitura.

> Projeto original: nenhum código, arte ou recurso do Finale é usado. A
> inspiração é só no modo de trabalhar (ferramentas por modo, entrada rápida
> pelo teclado).

## Estado atual (v0.2)

- Modelo musical com durações exatas (frações), acordes, pausas, pontos e acidentes
- Várias pautas por sistema (piano, coro SATB, quarteto de cordas, etc.)
- Motor de gravação próprio: espaçamento proporcional, quebras de sistema e
  página, justificação, hastes, bandeirolas, linhas suplementares, acidentes
  com colunas, segundas em acordes, armaduras e fórmulas de compasso
- Desenho com a fonte SMuFL **Bravura**: todos os símbolos de uma página saem
  numa única chamada de desenho, e só as páginas visíveis são desenhadas
- Interface organizada como a do Finale: paleta principal de ferramentas, menu da ferramenta ativa, paleta de Entrada simples, Entrada rápida pelo teclado (F1 mostra os atalhos), seleção de compassos, visualização de página/contínua (Ctrl+E), barra de navegação
- Ferramentas de Pauta, Clave, Armadura, Fórmula de compasso (com redistribuição das notas) e Compasso
- Desfazer/refazer ilimitado
- Formato próprio `.pauta` (XML aberto, veja `docs/FORMATO.md`)
- Exportação em PDF vetorial e impressão
- Barras de ligação e ligaduras de valor
- Quiálteras (Ctrl+3 tercina, Ctrl+5 quintina…) com número e colchete
- Reprodução (Espaço): usa o sintetizador do próprio Windows (piano, cordas, sopros, coro conforme o nome da pauta); no Linux, um sintetizador interno
- Letra (várias estrofes, hífens e linhas de melisma) e cifras (C, F#m7b5, G/B…) com ♯/♭ de verdade; as pautas se afastam sozinhas para dar espaço
- Até 4 camadas (vozes) por pauta, como no Finale: Alt+1–4 ou o seletor "Camada" na barra inferior
- Repetições: ritornelos, casas de 1ª/2ª vez, Segno, Coda, Fine, D.C. e D.S. (a reprodução segue a ordem certa)
- Partes de cada instrumento ligadas à grade (o que muda na parte muda na grade), com pausas de vários compassos e exportação de todas as partes em PDF
- Teclado MIDI: toque as notas e acordes e eles entram na Entrada rápida
- Importa e exporta MusicXML (`.musicxml`, `.mxl` compactado e `.xml`): leva e traz partituras do Finale, Sibelius, MuseScore e Dorico

## Instalar (sem terminal)

Baixe a versão mais recente na página **Releases** do repositório:

- **Windows:** `Pauta.exe` → dois cliques → Avançar. Não pede senha de administrador.
  Se o Windows mostrar "O Windows protegeu o computador", clique em
  **Mais informações → Executar assim mesmo** (o instalador ainda não é assinado digitalmente).
- **Ubuntu / Debian:** `pauta_X.Y.Z_amd64.deb` → dois cliques (abre a Central de Aplicativos) → Instalar.
- **Qualquer Linux:** `Pauta-x86_64.AppImage` → botão direito → Propriedades →
  "Permitir executar como programa" → dois cliques.

## Atualizações

O Pauta confere sozinho, ao abrir, se saiu uma versão nova (no máximo uma vez por dia). Se tiver,
mostra o que mudou e atualiza com um clique: no Windows ele baixa e instala sozinho e abre de novo.
Também dá para conferir em **Ajuda → Verificar atualizações**, ou desligar a verificação automática ali.
A consulta vai só à página Releases do GitHub e não envia nenhum dado seu.

O histórico de mudanças fica em [`CHANGELOG.md`](CHANGELOG.md).

## Compilar

Requisitos: Visual Studio 2022 Build Tools (C++), CMake, Ninja e Qt 6.11 (MSVC 64-bit) em `C:\Qt\6.11.3\msvc2022_64`.

```bat
build.cmd          :: compila em build\Pauta.exe
build.cmd dist     :: e monta uma pasta portátil em build\dist
build.cmd installer :: e gera o instalador build\installer\Pauta.exe (precisa do Inno Setup 6)
```

Linha de comando:

```bat
Pauta.exe arquivo.pauta
Pauta.exe --export-pdf saida.pdf arquivo.pauta
Pauta.exe --export-png pagina1.png arquivo.pauta
Pauta.exe --bench 2000        :: mede o tempo de gravação de 2000 compassos
```

## Etapas

Veja `docs/ROADMAP.md`.

## Licenças de terceiros

- Fonte Bravura © Steinberg Media Technologies GmbH — SIL Open Font License 1.1 (`resources/fonts/OFL.txt`)
- Qt 6 — LGPL v3 (ligação dinâmica)
