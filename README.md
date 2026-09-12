# Notes

A dead-simple Markdown writing app built with Qt Quick and C++, themed from
[pywal](https://github.com/dylanaraps/pywal).

This is a fork of [omawrite](https://github.com/omacom/omawrite) with the
Omarchy theme integration replaced by pywal, and the Omarchy package repository
replaced by a plain user-local install.

## Install

```sh
./bin/install
```

That builds the app and installs it under `~/.local`:

| Path | What |
| --- | --- |
| `~/.local/bin/notes` | the binary |
| `~/.local/share/applications/notes.desktop` | launcher / dock entry, registered for Markdown files |
| `~/.local/share/icons/hicolor/…/notes-md.{svg,png}` | app icon, scalable plus rasterised sizes |

The icon is called `notes-md` rather than `notes` on purpose: icon themes are
searched before hicolor, and several of them — Papirus included — already ship a
`notes` icon (it is an alias for Standard Notes), which would quietly replace
this one. The icon is also compiled into the binary, so the window still has one
if no icon theme is configured.

No root, no `makepkg`, no extra repositories. `./bin/uninstall` takes it all
back out. `PREFIX=/usr/local sudo -E ./bin/install` installs system-wide instead.

Run `./bin/build` alone to just produce `build/notes`, and `./bin/test` for the
test suite.

## Theming

Colours come from pywal's current palette, read at startup and re-read whenever
it changes — run `wal -i some-wallpaper.jpg` and open windows re-theme
immediately, with no restart.

`~/.cache/wal/colors.json` is the source of truth, falling back to the plain
`~/.cache/wal/colors` file if your template set does not emit the JSON. The
palette maps onto the app like this:

| App colour | From |
| --- | --- |
| page background | `special.background` |
| body text | `special.foreground` |
| accent (links, buttons, match highlights) | first usable of `color4`, `color5`, `color6`, `color2`, `color3`, `color1` |
| markdown markers, quotes, placeholders | `color8`, or a foreground/background blend if that is too close to the page |
| selection fill | the accent, pulled back toward the page so selected text stays legible |
| code spans, search bar, file browser chrome | the page, lifted 8% toward the foreground |

Light versus dark is decided by the luminance of the wallpaper background, so
`wal -l` puts the app in light mode without any extra configuration.

Every dialog sets its own modal scrim. The Material style re-declares
`Overlay.modal` per dialog with a colour that is a *light* wash in dark mode, so
a window-level override is ignored and the page turns grey behind a dialog.

If pywal has never run, the app falls back to a built-in palette and follows the
XDG desktop portal's `color-scheme` instead.

## Opening and saving

Notes browses the filesystem itself rather than calling out to a platform file
dialog, so the picker carries the same palette as the editor. Qt's own fallback
dialog appears whenever no platform theme plugin is loaded, and it is styled by
Material rather than by the wallpaper — which is the look this replaces.

The browser has a places sidebar, a clickable breadcrumb, size and modified
columns, a Markdown/all-files toggle and a dotfiles toggle. `Enter` opens or
descends, `Backspace` goes up a level, `Esc` cancels, and double-click does the
obvious thing. The sidebar and toggles fold away in a narrow window.

Both dialogs start in `~`. The one exception is Save As on a document that
already lives somewhere on disk, which starts in that document's folder so
saving a copy beside the original stays one step.

Save writes Markdown or plain text. The footer of the save view carries a
format toggle (`Markdown .md` / `Plain text .txt`) that rewrites the extension on
the name; typing an extension yourself still wins, so `.org` or `.conf` survive
untouched. New documents still default to `.md`.

The footer of the editor has three buttons: Save, **Save As** and Open. Save As
(also `Ctrl+Shift+S`) writes an already-open document to a new file, starting in
that document's own folder.

Saving over an existing file asks first. The platform dialog used to do that on
our behalf, so the in-app browser has to.

`open()` refuses anything that is not a text document — directories, devices
like `/dev/zero` that would never finish reading, files over 64 MiB, and
anything containing a NUL byte. Loading binary content would fill the editor
with replacement characters and write that corruption back on the next save.

It is not confined to `$HOME`: the sidebar ends with a **Filesystem** entry for
`/`, and the up button walks past the home directory all the way to the root, so
`/etc`, `/usr/bin` and the rest are reachable. Most files outside `$HOME` have no
Markdown extension, so flip the filter to **All files** to see them. Notes opens
them read-write like any other file — saving over a root-owned file will fail on
permissions, as it should.

## Shortcuts

- `Ctrl+S` saves. Unsaved documents use the XDG desktop portal file picker.
- `Ctrl+Shift+S` saves as.
- `Ctrl+O` opens a Markdown file through the portal picker.
- `Ctrl+P` opens the system print dialog.
- `Ctrl+N` opens a new Notes window.
- `Ctrl+Z`, `Ctrl+Shift+Z`, and `Ctrl+Y` handle undo and redo.
- `Super+F` toggles fullscreen. Qt maps this key as `Meta+F`.
- `Ctrl+F` searches the document. Use `Enter` or `Ctrl+G` for the next match and `Shift+Enter` for the previous match.
- `Ctrl+H` opens find and replace.
- `Ctrl+B`, `Ctrl+I`, and `Ctrl+K` insert bold, italic, and link Markdown.
- `Ctrl+?` shows the keyboard shortcut reference.

Unsaved drafts are recovered after an abnormal exit. Notes also watches open
files and warns before an external change can replace local work.

Text follows the desktop text size — GNOME's `text-scaling-factor`, read over
the desktop portal — and re-flows without a restart. The default of 12px leaves
Notes at the size it is designed around; larger and smaller sizes scale from
there.

## Requirements

- Qt 6: `qt6-base`, `qt6-declarative`, `qt6-svg`
- `librsvg` (optional), so the installer can rasterise PNG icon sizes

The iA Writer Mono font is bundled under the SIL Open Font License 1.1; see
`fonts/OFL.txt`. The font is copyright Information Architects Inc. and based on
IBM Plex, copyright IBM Corp.
