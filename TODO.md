# TODO

## CI / Tooling

- [x] **CI warnings — Node.js 20 deprecated on GitHub Actions**
  `actions/checkout@v4`, `actions/setup-python@v5`, `actions/upload-artifact@v4`
  are being forced to Node.js 24. `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true` is
  already set so the builds pass, but the warnings will persist until the action
  versions that natively target Node.js 24 are pinned (e.g. `actions/checkout@v5`
  once stable). Track: https://github.blog/changelog/2025-09-19-deprecation-of-node-20-on-github-actions-runners/
