# Standalone skin packages

Follow the mandatory independent-skin boundary in the repository `AGENTS.md`
and `docs/SKINS.md`.

Skin work belongs in its own package folder and must run on the unchanged
released uFlow skin API. Do not change app core files, shared renderers/parsers,
settings, installer defaults, dependency pins, or app build files to support
a skin. Do not bundle it into the app. Unsupported design requirements must be
reported; core/API extensions require a separate explicit user request.

Keep this instruction file outside individual package folders. Only manifests,
supported layouts and assets belong inside a distributable skin package.
