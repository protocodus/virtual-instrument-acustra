# Research and validation scripts

Scripts, probes and patches that produced dated evidence under `Docs/`. Each
folder carries the stem of the report or evidence folder it belongs to:
`Tools/research/<stem>/` holds the code for `Docs/research/<stem>.md` and
`Docs/validation/<stem>/`, with the same subfolders it had there.

They were moved out of `Docs/` on 2026-10-17 as pure renames, byte for byte,
because receipts record their SHA-256. They are kept as executed, not
maintained: path constants inside them (`Path(__file__)` parents, `Docs/...`
locations, sibling data files) describe the layout at the time they ran. To
rerun one, copy it next to its evidence folder or adjust those paths in the
copy. None is built or registered with CTest.
