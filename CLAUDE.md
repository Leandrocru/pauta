# Pauta — notas para o Claude Code

Editor de partituras em C++20 + Qt 6 Widgets, inspirado no fluxo de trabalho do Finale.
Metas: rodar bem em PCs fracos (i5 de 6ª geração, 8 GB) e abrir muitos formatos.
Interface e mensagens em **português do Brasil**. Comentários no código em inglês.

## Regras do projeto
- Nada de código, arte ou recursos do Finale (nem decompilar). Só a ideia do fluxo de trabalho.
- Durações sempre em `Frac` (frações exatas), nunca `float`.
- Símbolos musicais só pela fonte SMuFL Bravura (`render/Smufl.*`).
- Desempenho é requisito: meça com `--bench` antes/depois de mexer em `render/`.
- Roteiro em `docs/ROADMAP.md` (marque o que concluir). Formato `.pauta` em `docs/FORMATO.md`.

## Arquitetura (`src/`)
- `model/` — `Score → Staff → Measure → Event(pitches|rest)`; `Measure` tem 4 camadas (`events` = camada 1, `voice(v)` acessa qualquer uma); utilitários de altura/clave/armadura.
- `render/Engraver` — função pura `Score → Layout` (espaçamento, sistemas, páginas, primitivas por página).
- `render/Renderer` — pinta um `PageLayout` (glifos em um único `QGlyphRun`), exporta PDF/PNG.
- `edit/Editor` — cursor, entrada estilo Speedy Entry, desfazer por snapshot (`QUndoStack`).
- `ui/` — `MainWindow` (menus, paletas, barra de navegação), `ScoreView` (só pinta páginas visíveis; mouse e teclado conforme a ferramenta), `Tools.h` (lista de ferramentas), `Icons` (ícones próprios desenhados em tempo de execução), `LaunchDialog`, `NewScoreDialog`.
- Interface: organização inspirada no Finale, mas ícones e arte são próprios (nunca copiar os do Finale).
- `playback/` — `Sequence` (Score → mensagens MIDI com tempo; ligaduras viram uma nota só), `Player` (timer de 4 ms), `MidiOut` (Windows: WinMM/GS Wavetable; Linux: `SoftSynth`, sintetizador de tabela de onda via ALSA). Teste sem som: `--playback-check [arquivo]`.
- `io/NativeFormat` — leitura/gravação do `.pauta` (XML).
- `io/MusicXml` — importação MusicXML (partwise; `.mxl` via `QZipReader` privado do Qt). Cada voz MusicXML de uma pauta vira uma camada (até 4; mapeamento fixo por parte) e avisa o que simplificou. Arquivo de teste: `tests/data/piano-teste.musicxml` (+ `.mxl`), usado também no CI.

## Compilar
- **Windows** (máquina do dono): `build.cmd` (Release em `build\Pauta.exe`), `build.cmd dist` (pasta portátil).
  Qt em `C:\Qt\6.11.3\msvc2022_64`; se faltar, `tools/get-qt.sh` baixa e confere.
- **Linux / sessão na nuvem**: `sudo apt-get install -y qt6-base-dev ninja-build cmake` e então
  `cmake -S . -B build -G Ninja && cmake --build build`.
- CI: `.github/workflows/build.yml` compila Windows e Linux e gera o instalador `Pauta.exe` (Inno Setup, `packaging/windows/pauta.iss`), o `.deb` (CPack) e o AppImage (linuxdeploy). Uma tag `vX.Y.Z` publica tudo em Releases; suba a versão em `CMakeLists.txt` antes.
- No Linux o executável se chama `pauta` (no Windows, `Pauta.exe`).
- `build.cmd installer` gera o instalador localmente (precisa do Inno Setup 6).

## Verificar sem interface gráfica
```
QT_QPA_PLATFORM=offscreen ./build/pauta --export-png demo.png            # partitura de demonstração
QT_QPA_PLATFORM=offscreen ./build/pauta --bench 1000                     # tempo de gravação
QT_QPA_PLATFORM=offscreen ./build/pauta --empty --test-keys "C D E F 4 G + Return" \
    --grab janela.png --save-to teste.pauta                               # simula teclado e captura a janela
# --tool N escolhe a ferramenta (índice em ui/Tools.h; 0 Seleção, 8 Entrada simples, 9 Entrada rápida),
# --scroll-view liga a visualização contínua, --select-all seleciona tudo.
```
Olhe as imagens geradas para conferir a gravação. No Windows, ponha `C:\Qt\6.11.3\msvc2022_64\bin` no PATH.
