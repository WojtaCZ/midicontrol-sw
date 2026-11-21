# Czech user manual (MkDocs + PDF)

- Chapters: `docs/*.md` (order = `nav` in `mkdocs.yml`). Facts with their sources: `facts/{remote,base,pc}.md`
  (not published). Every user-visible change updates the chapter and the facts file in the same change.
- Build from the repo root: `docs-build` (site in `docs/build/site/`, PDF in `docs/build/manual.pdf`).
  Check the PDF: `pdftoppm -png -r 60 docs/build/manual.pdf <dir>/pg` and look at the pages.

## Re-export the pictures (one command per source)

```sh
M=/workspaces/MIDICONTROL-b81bba4a          # repos side by side
$M/midicontroller-fw/tools/screens/export.sh $M/midicontrol-sw/docs/manual/docs/img/screens/remote
$M/midicontrol-fw/tools/screens/export.sh    $M/midicontrol-sw/docs/manual/docs/img/screens/base
```

The export runs the real GUI code on the host (scenarios in `tools/screens/main.cpp` of each firmware repo) and
keeps the file names, so the chapters do not need edits unless a screen is added or renamed. Web screenshots in
`docs/img/web/` were taken from an offline PC run with a fake base (see `tools/systest/README.md`).
