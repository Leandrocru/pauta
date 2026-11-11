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

## Instalar (sem terminal)

Baixe a versão mais recente na página **Releases** do repositório:

- **Windows:** `Pauta.exe` → dois cliques → Avançar. Não pede senha de administrador.
  Se o Windows mostrar "O Windows protegeu o computador", clique em
  **Mais informações → Executar assim mesmo** (o instalador ainda não é assinado digitalmente).
- **Ubuntu / Debian:** `pauta_X.Y.Z_amd64.deb` → dois cliques (abre a Central de Aplicativos) → Instalar.
- **Qualquer Linux:** `Pauta-x86_64.AppImage` → botão direito → Propriedades →
  "Permitir executar como programa" → dois cliques.

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
