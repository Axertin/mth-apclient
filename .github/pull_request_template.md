## What this changes

<!-- What the change does and why. Link the issue it closes ("Closes #123") if there is one. -->

## How it was tested

<!-- Unit tests, a build preset, in-game on Linux or Windows, et cetera -->

---

**The title of this PR has to follow [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/).** It becomes the subject of the squash-merge commit, and release-please parses it to build the changelog, so a title it cannot parse leaves the change out of the release notes.

Format is `type: description`, with no scope in parentheses. The types the changelog recognizes are `feat`, `fix`, `perf`, `refactor`, `build`, `ci`, `docs`, `test`, `style`, and `chore`. Append `!` to the type for a breaking change (`feat!: ...`).

The individual commits inside the branch are exempt. Only the title is read.
