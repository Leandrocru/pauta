# Novidades do Pauta

Todas as mudanças de cada versão. A mais recente fica em cima.

## [0.7.0] — 2026-11-12

### ✨ Novidades
- **Aviso de versão nova:** ao abrir, o Pauta confere se saiu uma versão nova e mostra o que mudou.
- **Atualizar com um clique:** no Windows, o Pauta baixa e instala a versão nova sozinho e abre de novo já atualizado. No Linux, atualiza o AppImage ou abre o `.deb` novo na Central de Aplicativos.
- **"O que há de novo":** depois de atualizar, uma janela mostra as novidades da versão. Ela também está em **Ajuda → O que há de novo**.
- **Ajuda → Verificar atualizações:** confere na hora. Há uma opção para desligar a verificação automática.

## [0.6.1] — 2026-11-12

### 🐛 Correções
- **Entrada simples:** agora mostra a nota inteira (cabeça, haste e bandeirola, ou a pausa), em roxo, **exatamente onde ela vai entrar**, como no Finale.
  - Com o mouse sobre a pauta, a nota substitui o ponteiro e se encaixa no ponto de inserção.
  - Sobre uma nota existente, indica que o clique vai formar um acorde.
  - Sem mouse, o cursor do teclado também tem a forma da nota.

## [0.6.0] — 2026-11-11

### ✨ Novidades
- **Reprodução de som:** **Espaço** toca a partir do compasso atual (ou da seleção) e para; **Ctrl+Home** volta ao início.
  - O compasso que está tocando fica destacado e a tela acompanha.
  - Timbre escolhido pelo nome da pauta: piano, violino, flauta, violão, coro…
  - Windows: usa o sintetizador do próprio sistema. Linux: usa um sintetizador embutido.
- **Andamento** (♩ =) editável na barra inferior e salvo na partitura.
- **Quiálteras:** **Ctrl+3** tercina, **Ctrl+5** quintina, **Ctrl+6** sextina… Mostram número e colchete, e são importadas do MusicXML.

## [0.5.0] — 2026-11-11

### ✨ Novidades
- **Várias vozes:** 4 camadas por pauta, como no Finale (**Alt+1 a 4** ou o seletor na barra inferior).
  - Com duas camadas, as hastes se separam (camada 1 para cima, camada 2 para baixo).
  - Notas que se encostam são deslocadas, e cada camada tem a sua cor.
- **MusicXML:** cada voz do arquivo vira uma camada; antes só a primeira era mantida.

## [0.4.0] — 2026-11-11

### ✨ Novidades
- **Importar MusicXML** (`.musicxml`, `.mxl` compactado e `.xml`): o caminho para trazer partituras do Finale, Sibelius e MuseScore.
- O Pauta aparece em "Abrir com" para arquivos MusicXML.

## [0.3.0] — 2026-11-11

### ✨ Novidades
- **Barras de ligação** automáticas, com barras secundárias e parciais.
- **Ligaduras de valor** (`=` ou `L`), inclusive entre compassos e quebras de linha.
- **Instaladores:** `Pauta.exe` para Windows, `.deb` e AppImage para Linux.

### 🔧 Melhorias
- Espaço vertical ajustado para notas muito agudas ou graves.

## [0.2.0] — 2026-11-11

### ✨ Novidades
- **Interface no estilo do Finale:** paleta principal de ferramentas, menu da ferramenta ativa, paleta de Entrada simples, moldura da Entrada rápida, seleção de compassos, visualização contínua (**Ctrl+E**), barra de navegação, janela inicial e impressão.

## [0.1.0] — 2026-11-10

### ✨ Novidades
- Primeira versão: gravação de partituras em páginas, Entrada rápida pelo teclado, desfazer e refazer, formato `.pauta` e exportação em PDF.
