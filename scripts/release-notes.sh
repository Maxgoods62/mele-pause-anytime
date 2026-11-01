#!/usr/bin/env bash
# Prints the notes of GitHub release $1 (v<N>): that version's CHANGELOG.md entry, the hashes in dist/SHA256SUMS, and how
# to check a download.
#   scripts/release-notes.sh v18 > dist/release-notes.md
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="${1:?usage: scripts/release-notes.sh v<N>}"
die() { echo "release-notes.sh: $*" >&2; exit 1; }

ENTRY="$(awk -v head="## $TAG " 'index($0, head) == 1 { on = 1; next } /^## / { on = 0 } on' "$HERE/CHANGELOG.md")"
[[ -n "${ENTRY//[$'\n ']/}" ]] || die "no '## $TAG ' entry in CHANGELOG.md"
[[ -s "$HERE/dist/SHA256SUMS" ]] || die "no dist/SHA256SUMS: run package.sh first"

printf '%s\n\n' "$ENTRY"
echo '## Check your download'
echo
echo '| File | SHA-256 |'
echo '|---|---|'
while read -r hash name; do
    printf '| `%s` | `%s` |\n' "${name#\*}" "$hash"
done < "$HERE/dist/SHA256SUMS"
cat <<EOF

In PowerShell, compare the hash with the table above:

\`\`\`powershell
Get-FileHash .\\LE1MelePause-$TAG.zip -Algorithm SHA256
\`\`\`

With the [GitHub CLI](https://cli.github.com), check that this repo's workflow built the file from the tagged source:

\`\`\`
gh attestation verify LE1MelePause-$TAG.zip -R Maxgoods62/mele-pause
\`\`\`
EOF
