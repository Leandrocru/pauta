# Novidades do Pauta

Todas as mudanças de cada versão. A mais recente fica em cima.

## [0.13.0] — 2026-11-13

### ✨ Novidades
- **Ferramenta Repetição:**
  - **Clique num compasso:** abre uma janela com ritornelo de início e de fim (e quantas vezes repete), casa de 1ª/2ª vez, Segno, Coda, instrução no fim do compasso (Fine, D.C., D.S., D.C./D.S. al Fine ou al Coda, To Coda) e barra dupla ou final.
  - **Clique rápido:** o menu **Repetição** tem cada sinal. Escolha um e clique nos compassos para pôr ou tirar.
  - **Seleção:** **Repetir compassos selecionados** põe |: e :| em volta da seleção.
  - **Reprodução:** segue as repetições, as casas e os saltos na ordem certa.
  - **MusicXML:** as repetições são importadas e exportadas.
- **Partes ligadas à grade:**
  - **Escolher a parte:** use **Ver** na barra de baixo ou **Documento → Editar parte**.
  - **Ligação:** a parte mostra só aquele instrumento, com o nome no alto. O que você muda na parte muda a grade, e vice-versa.
  - **Pausas de vários compassos:** compassos vazios seguidos viram uma pausa só, com o número em cima.
  - **PDF e impressão:** **Exportar → PDF** e **Imprimir** saem como está na tela (grade ou parte).
  - **Todas as partes:** **Exportar → Partes em PDF** cria um arquivo para cada instrumento.
- **Teclado MIDI:**
  - **Ligar:** ligue o teclado e escolha-o em **MIDI/Áudio → Teclado MIDI (entrada)**, ou clique na ferramenta **Teclado MIDI**.
  - **Escrever:** na **Entrada rápida**, cada nota tocada entra no cursor com a duração escolhida. Teclas tocadas juntas viram acorde, e ♯/♭ seguem a armadura.
  - **Som:** as notas soam no computador com o instrumento da pauta.
  - **Lembrar:** o teclado escolhido fica guardado para a próxima vez.

## [0.12.0] — 2026-11-13

### ✨ Novidades
- **Ferramenta Letra:** clique numa nota e digite a sílaba.
  - **Espaço** ou **Tab** vai para a próxima nota (pausas são puladas); **Enter** e as setas ← → também andam.
  - **-** separa sílabas de uma palavra: o hífen aparece entre elas.
  - **_** estende a sílaba por várias notas (melisma), com a linha de extensão embaixo.
  - **↑ ↓** troca de estrofe; o menu **Letra** também escolhe a estrofe 1 a 4.
  - **Backspace** apaga; **Esc** termina.
- **Ferramenta Cifra:** clique numa nota e digite o acorde (C, Am, G7, F#m7b5, Bbmaj7, G/B…). Os # e b viram ♯ e ♭ na partitura.
- **Espaço automático:** as pautas se afastam sozinhas quando há letra embaixo ou cifras em cima, só nas linhas que precisam.
- **MusicXML:** letra (com estrofes, hífens e melismas) e cifras passam a ser importadas e exportadas.

## [0.11.0] — 2026-11-13

### ✨ Novidades
- **Mudanças no meio da peça:** fórmula de compasso, tonalidade e clave podem mudar em qualquer compasso.
  - **Ferramentas Fórmula de compasso, Armadura e Clave:** clique no compasso onde a mudança começa. A janela já vem com "A partir do compasso" preenchido e vale até a próxima mudança.
  - **Fórmula:** só o trecho até a próxima mudança tem as notas redistribuídas; o número de compassos se ajusta sozinho.
  - **Tonalidade:** as notas daquele trecho acompanham a nova armadura.
  - **Desenho:** a mudança aparece no compasso, com bequadros cancelando a armadura antiga. Quando cai no começo de uma linha, entra no cabeçalho da linha.
  - **Reprodução:** respeita os compassos de tamanhos diferentes.
  - **MusicXML:** as mudanças passam a ser importadas e exportadas; antes eram ignoradas com aviso.

## [0.10.0] — 2026-11-13

### ✨ Novidades
- **Exportar MusicXML** (**Arquivo → Exportar → MusicXML**, Ctrl+Shift+X), em `.musicxml` ou `.mxl` compactado. Assim as partituras do Pauta abrem no Finale, MuseScore, Sibelius, Dorico e outros.
  - **Estrutura:** partes (piano em duas pautas numa parte só), armadura, fórmula de compasso, claves e andamento.
  - **Notas:** acordes, pausas, pontos, acidentes, as 4 camadas e barras de ligação.
  - **Marcas:** ligaduras de valor e de expressão, quiálteras, articulações, fermatas, dinâmicas, crescendo e diminuendo.
- Os arquivos seguem o **padrão oficial MusicXML 4.0** e são conferidos contra ele a cada versão.

## [0.9.0] — 2026-11-13

### ✨ Novidades
- **Ligaduras de expressão** sobre várias notas.
  - Ficam do lado oposto às hastes, sobem para passar por cima das notas do meio e se dividem em duas quando cruzam uma quebra de linha.
  - Se alguma nota sob a ligadura tem haste para baixo, ela vai por cima.
- **Crescendo e diminuendo** (os "grampos"), alinhados com as dinâmicas e começando depois delas.
- **Ferramenta Linhas e ligaduras** (paleta principal): escolha o tipo e clique na primeira nota e depois na última.
- **Na paleta (agora chamada "Expressões"):** com um trecho selecionado, os botões de ligadura, crescendo e diminuendo vão da primeira à última nota da seleção. O menu da ferramenta também remove essas linhas.
- **Reprodução:** crescendo e diminuendo mudam o volume aos poucos até a próxima dinâmica.
- **MusicXML:** ligaduras (`slur`) e grampos (`wedge`) passam a ser importados.

## [0.8.0] — 2026-11-12

### ✨ Novidades
- **Articulações:** staccato, acento, tenuto, marcato, staccatissimo e fermata.
  - Ficam do lado da cabeça da nota, nunca sobre as linhas da pauta.
  - O marcato e a fermata vão sempre acima.
- **Dinâmicas:** *ppp* a *fff*, *sfz*, *sf* e *fp*, abaixo da pauta.
- **Ferramentas Articulação e Expressão** (paleta principal): escolha o sinal na nova paleta **Articulações e dinâmicas** e clique nas notas. Clicar de novo remove.
- **Paleta Articulações e dinâmicas:** fora dessas ferramentas, o clique aplica na nota do cursor ou em todos os compassos selecionados.
- **Reprodução expressiva:**
  - a dinâmica controla o volume;
  - acento e marcato tocam mais forte;
  - staccato soa curto e tenuto sustenta.
- **MusicXML:** articulações, fermatas e dinâmicas passam a ser importadas.

## [0.7.1] — 2026-11-12

### 🔧 Melhorias
- **Entrada simples pelo teclado, como no Finale:** a nota roxa agora segue as setas.
  - **↑ / ↓** mudam a altura (**Ctrl** muda a oitava) e **← / →** levam a nota roxa de nota em nota.
  - **1–8** escolhem a duração da nota roxa, sem inserir. Sobre uma nota existente, mudam a duração dela.
  - **Enter** ou **A–G** inserem a nota; Enter sobre uma nota existente acrescenta ao acorde.
  - **Shift + A–G** acrescenta a nota ao acorde anterior, e **0** ou **R** inserem pausa.
- Ao usar o teclado, a nota roxa deixa de seguir o mouse e volta para o cursor.

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
