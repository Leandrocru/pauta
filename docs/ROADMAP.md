# Roteiro

Cada etapa termina com um programa utilizável.

## Etapa 1 — Base ✅ (em andamento)
- [x] Modelo musical (pautas, compassos, acordes, pausas, pontos, acidentes)
- [x] Gravação: espaçamento, sistemas, páginas, hastes, bandeirolas, suplementares
- [x] Fonte SMuFL (Bravura), desenho em lote, só páginas visíveis
- [x] Speedy Entry (teclado), clique para posicionar o cursor, zoom
- [x] Desfazer/refazer, formato `.pauta`, exportar PDF
- [x] Barras de ligação (agrupadas por tempo, secundárias e parciais, inclinação limitada)
- [x] Ligaduras de valor (= ou L), inclusive entre sistemas
- [x] Quiálteras (Ctrl+3, Ctrl+5…; número e colchete; importadas do MusicXML)

## Interface no estilo do Finale ✅ (v0.2)
- [x] Paleta principal de ferramentas (ferramenta ativa decide o clique; as não prontas aparecem desativadas)
- [x] Menu da ferramenta ativa na barra de menus
- [x] Menus Arquivo, Editar, Utilitários, Exibir, Documento, MIDI/Áudio, Plug-ins, Janela, Ajuda
- [x] Paleta de Entrada simples (encaixável/flutuante) com nota fantasma sob o mouse
- [x] Moldura de edição da Entrada rápida
- [x] Seleção de compassos (clique, arraste, Shift+clique), limpar conteúdo
- [x] Ferramentas Zoom, Mão, Pauta, Clave, Armadura, Fórmula de compasso, Compasso
- [x] Visualização de página / contínua (Ctrl+E)
- [x] Barra inferior: página, compasso, zoom, controles de reprodução (desativados)
- [x] Janela inicial (assistente, em branco, abrir, demonstração) e impressão

## Etapa 2 — Formatos principais
- [x] MusicXML (`.musicxml` / `.mxl`) — importar (até 4 vozes por pauta viram camadas; avisa o que simplificou)
- [x] MusicXML — exportar (.musicxml e .mxl, validado pelo esquema oficial 4.0)
- [ ] MIDI (`.mid`) — importar (com quantização) e exportar
- [x] Reprodução: sintetizador MIDI do Windows / sintetizador interno no Linux; Espaço toca/para; andamento
- [ ] Reprodução com SoundFont próprio (timbres melhores e iguais em todos os sistemas)
- [ ] Exportar SVG / PNG por página

## Etapa 3 — Gravação completa
- [ ] Mudança de fórmula, armadura e clave no meio da partitura
- [x] Várias vozes por pauta: 4 camadas como no Finale (Alt+1–4), hastes por camada, notas deslocadas em uníssono/segunda, acidentes compartilhados
- [x] Dinâmicas e articulações (ferramentas Expressão e Articulação, paleta, reprodução)
- [x] Ligaduras de expressão e crescendo/diminuendo (ferramenta Linhas e ligaduras)
- [ ] Chaves e colchetes de grupo, abreviações de instrumento
- [ ] Layout incremental (re-gravar só o sistema afetado)

## Etapa 4 — Ferramentas no estilo Finale
- [ ] Ferramentas por modo: Expressão, Articulação, Letra, Acordes, Repetição
- [ ] Partes extraídas vinculadas à grade
- [ ] HyperScribe-like: entrada por teclado MIDI
- [ ] Simple Entry com paleta completa

## Etapa 5 — Mais formatos
- [ ] Finale `.musx` (via biblioteca aberta musxdom)
- [ ] MEI, ABC, Humdrum (via Verovio)
- [ ] MuseScore `.mscz`, Guitar Pro, Capella `.capx`
- [ ] Exportar LilyPond, áudio WAV/MP3
- [ ] OMR opcional (Audiveris) para PDF/foto

## Metas de desempenho (i5-6200U, 8 GB)
- Abrir 100 páginas em < 1 s
- < 150 MB de RAM
- Editar uma nota e ver o resultado na hora (< 16 ms)
