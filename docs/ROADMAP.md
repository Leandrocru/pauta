# Roteiro

Cada etapa termina com um programa utilizável.

## Etapa 1 — Base ✅ (em andamento)
- [x] Modelo musical (pautas, compassos, acordes, pausas, pontos, acidentes)
- [x] Gravação: espaçamento, sistemas, páginas, hastes, bandeirolas, suplementares
- [x] Fonte SMuFL (Bravura), desenho em lote, só páginas visíveis
- [x] Speedy Entry (teclado), clique para posicionar o cursor, zoom
- [x] Desfazer/refazer, formato `.pauta`, exportar PDF
- [ ] Barras de ligação (colchetes de colcheias)
- [ ] Ligaduras de valor (L) e quiálteras

## Etapa 2 — Formatos principais
- [ ] MusicXML (`.musicxml` / `.mxl`) — importar e exportar
- [ ] MIDI (`.mid`) — importar (com quantização) e exportar
- [ ] Reprodução com SoundFont (carregamento sob demanda)
- [ ] Exportar SVG / PNG por página

## Etapa 3 — Gravação completa
- [ ] Mudança de fórmula, armadura e clave no meio da partitura
- [ ] Várias vozes por pauta (camadas, como no Finale)
- [ ] Ligaduras de expressão, dinâmicas, articulações
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
