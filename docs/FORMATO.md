# Formato `.pauta` (versão 1)

XML UTF-8, legível e editável à mão.

```xml
<pauta version="1">
 <score>
  <title>Estudo</title>
  <composer>Fulano</composer>
  <time beats="3" beatType="4"/>
  <key fifths="1"/>                     <!-- -7..7: bemóis negativos, sustenidos positivos -->
  <staff name="Piano" clef="treble">   <!-- treble | bass | alto | tenor -->
   <measure>
    <chord dur="2">                    <!-- dur: -1 breve, 0 semibreve, 1 mínima, 2 semínima, 3 colcheia ... 6 semifusa -->
     <note step="G" oct="4"/>
     <note step="B" oct="4"/>
    </chord>
    <rest dur="2" dots="1"/>
   </measure>
  </staff>
 </score>
</pauta>
```

- `<tempo qpm="96"/>` em `<score>`: andamento para a reprodução (semínimas por minuto).
- `tuplet="3:2"` em `<chord>`/`<rest>`: quiáltera (3 no tempo de 2); `tupletStart="1"` marca o início do grupo.
- `artic="staccato,accent"` em `<chord>`/`<rest>`: articulações (`staccato`, `accent`, `tenuto`, `marcato`, `staccatissimo`, `fermata`).
- `dyn="mf"` em `<chord>`/`<rest>`: dinâmica que começa nessa nota (`ppp`…`fff`, `sfz`, `sf`, `fp`).
- `slurStart="1"` / `slurEnd="1"` em `<chord>`/`<rest>`: começo e fim de uma ligadura de expressão (o começo se liga ao próximo fim na mesma camada).
- `hairpin="cresc|dim"` / `hairpinEnd="1"`: começo e fim de crescendo/diminuendo.
- `chordSymbol="C7"` em `<chord>`/`<rest>`: cifra acima da nota (como foi digitada; `#` e `b` viram ♯ e ♭ na tela).
- `<lyric verse="1" text="can" hyphen="1" extender="1"/>` dentro de `<chord>`: sílaba da letra (estrofe, hífen para a próxima sílaba, linha de melisma).
- `tie="1"` em `<note>`: ligadura de valor com a mesma nota no próximo evento da pauta.
- `alter` em `<note>`: -2..2 (bemol duplo … sustenido duplo). A nota escrita é
  sempre explícita; a armadura só decide quais acidentes aparecem.
- Camadas (vozes, como no Finale): a camada 1 fica direto em `<measure>`; as camadas 2–4 ficam em `<voice n="2">…</voice>` dentro do compasso.
- Mudanças no meio da peça ficam no `<measure>` onde começam: `time="3/4"` e `key="-3"` (no compasso da primeira pauta) e `clef="bass"` (no compasso da pauta que muda).
- Todas as pautas têm o mesmo número de compassos.
- Arquivos com `version` maior que a suportada são recusados.
