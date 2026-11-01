# Releasing MelePause

[`.github/workflows/build.yml`](../.github/workflows/build.yml) builds and publishes MelePause on GitHub's Windows runner,
with the same `package.sh` and `build.sh` you run at home.

| Push | Build | Published at |
|---|---|---|
| `main` | `LE<n>MelePause-latest.zip`, `SHA256SUMS` | <https://maxgoods62.github.io/mele-pause/latest/>, replaced by each push |
| tag `v<N>` | `LE<n>MelePause-v<N>.zip`, `SHA256SUMS` | [Releases](https://github.com/Maxgoods62/mele-pause/releases), never replaced |

Each run's summary shows the hashes. The run also keeps the `.pdb` files for 90 days, as the `pdb` artifact: only these
match that build's ASI.

## First setup

Run this once, right after the repo goes public and before the first tag:

```bash
scripts/protect-repo.sh
```

## Publish a version

1. Set `ASI_VERSION` and its two strings in `src/SharedVersion.h`, and date its entry in `CHANGELOG.md`.
2. Push `main`, and test the zips from the latest page in the game.
3. Tag that commit and push the tag:

   ```bash
   git tag -a v19 -m "MelePause v19"
   git push origin v19
   ```

The workflow stops, and publishes nothing, when the tag differs from `ASI_VERSION`, when the commit is not on `main`, or
when `CHANGELOG.md` has no entry for it. A tag can't be taken back: a bad version is fixed by the next number.

## Check a download

The hashes are SHA-256, in `SHA256SUMS` and in the release notes. A hash shows that the file matches the list. To check
one in PowerShell:

```powershell
Get-FileHash .\LE1MelePause-v19.zip -Algorithm SHA256
```

A stronger check shows that this repo's workflow built the file, from which commit. It works on latest zips too. It needs
the [GitHub CLI](https://cli.github.com):

```bash
gh attestation verify LE1MelePause-v19.zip -R Maxgoods62/mele-pause
```

For a release, `gh release verify-asset v19 LE1MelePause-v19.zip -R Maxgoods62/mele-pause` also checks that the file is
that release's.

## What stops a rebuilt tag

| Setting | Stops |
|---|---|
| Immutable releases | Changing or deleting a published release's files or moving its tag. If the release itself is deleted, its tag name can't be used again. |
| Ruleset `release tags are final` | Moving or deleting a `v*` tag, for everyone, the admin included. |
| Ruleset `only admins create release tags` | A collaborator creating a `v*` tag. |
| `gh release create` | Re-running a tag's workflow: it refuses a tag that has a published release. |
| Attestation | A file swapped outside the workflow going unnoticed: the signature, in a public log, is tied to the file's hash. |
| Ruleset `main` | Force pushes to `main` and deleting it. Direct pushes stay allowed. |
| Actions policy | Any action not from GitHub, or not pinned to a commit. |

The workflow runs nothing on pull requests, and the build job has no write permission and no cache. Keep the `uses:` lines
pinned to full commits when you update them.
