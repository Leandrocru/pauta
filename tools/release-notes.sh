#!/usr/bin/env bash
# Texto da página de uma versão no GitHub: a seção do CHANGELOG.md + como instalar.
# Uso: tools/release-notes.sh 0.7.0 > notas.md
set -euo pipefail
ver="${1#v}"
here="$(cd "$(dirname "$0")/.." && pwd)"

# Seção "## [ver] ..." até a próxima "## [", sem a linha do título.
awk -v v="$ver" '
  index($0, "## [" v "]") == 1 { on = 1; next }
  on && /^## \[/ { exit }
  on { print }
' "$here/CHANGELOG.md" | sed -e '/./,$!d'

cat <<'EOF'

---

### 📥 Como instalar
- **Windows:** baixe `Pauta.exe` e dê dois cliques (não precisa de senha de administrador). Se aparecer "O Windows protegeu o computador", clique em **Mais informações → Executar assim mesmo**.
- **Ubuntu / Debian:** baixe o `.deb` e dê dois cliques (abre a Central de Aplicativos).
- **Qualquer Linux:** baixe `Pauta-x86_64.AppImage`, clique com o botão direito → Propriedades → marque **Permitir executar como programa** e dê dois cliques.

Já tem o Pauta 0.7.0 ou mais novo? Ele avisa sozinho e atualiza com um clique.
EOF
