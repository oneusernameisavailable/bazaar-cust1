# Pre-Coding Gate

Before writing any code, answer these:

1. **Files** — Which specific files will be modified? (path + filename)
2. **Tests** — Which existing test covers the changed logic? If none, what test must be written?
3. **GTK4 widget impact** — Which accessible widgets (by name/role) are affected?
4. **API version check** — Does the change use any GTK4/libadwaita API below 4.22/1.8?
5. **Flatpak layer** — Does the change touch flatpak operations? If so, which file in src/flatpak/?
6. **Memory** — Are any new GObject allocations paired with _unref calls?
7. **Build** — Are any new source files added? Then meson.build must be updated.
8. **Dependencies** — Does this change require a new library dependency? If yes:
   - What pkg-config name?
   - What minimum version?
   - Check for version conflicts with existing deps in src/meson.build lines 4-19
   - Check for supply chain risk (provenance, maintenance status)
9. **Function signatures** — Does this change modify an existing function's signature or return type? If yes, list every file that calls it so all are updated together.

Approval required before coding.
