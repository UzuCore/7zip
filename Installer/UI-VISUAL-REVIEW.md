# Windows UI visual review (2026-10-29)

Environment: local Windows x64, Korean UI, 96 DPI / 100% scaling. Both Light
and Dark themes were captured from application windows without including other
applications. Current screenshots are in the local preview's `review` folder.

## Findings and fixes

| Finding | Change | Check |
| --- | --- | --- |
| Checkbox/radio focus outlines span the entire row | Measure the label and keep the keyboard focus cue around its text | Six settings pages, Light/Dark |
| Edit controls keep bright native frames | Gray unfocused frame, blue focused frame, subdued disabled frame | Editor paths, compression/password fields |
| Disabled combo text is too dim | Draw disabled selected text with readable gray; retain disabled behavior | Timestamp precision options |
| Hidden NTFS options leave a large blank area | Move the following controls up and shrink the dialog when all NTFS options are unsupported | 7z/ZIP compact; WIM keeps NTFS group |
| Timestamp precision string clips | Widen the precision combo; keep controls inside the dialog | Full Windows precision label in both themes |
| Alternative selection uses pale pink with light text | Use a dark burgundy background and keep the light foreground | Mark an item, move keyboard focus away, inspect both panels |

Button hover feedback and disabled checkbox/radio foregrounds were also aligned
with the dialog palette. Existing preferences used during the alternative
selection check were restored.

## Review matrix

- All six settings pages, each in Light and Dark.
- Compression options, expanded format dropdown, encrypted-archive password prompt.
- Advanced options for 7z, ZIP and WIM in both themes; every visible child control
  remained within the resized dialog client bounds.
- Progress with populated totals, rate, processed bytes and percentage. Pause /
  resume worked in both themes; the finished ZIP passed integrity validation.
- One- and two-panel file manager views, including marked and focused items.
- Actual GUI operations after accepting advanced options: 7z, ZIP, WIM, ALZ and
  EGG creation succeeded; all five archives passed integrity checks and their
  extracted test file matched the source bytes.
- A full Windows x64 rebuild succeeded with the project's warnings-as-errors
  settings. Transitive theme/header/resource edits require `nmake /A` with the
  upstream makefiles; extra prerequisites must not be passed through `$**` to RC.

## Limits

This is a 100% scaling review. Physical mixed-DPI / multi-monitor tests and
native ARM64 visual tests have not been performed. Windows-owned file pickers
and standard message boxes use the Windows appearance. The changes remain on
the local `ui-theme-preview` branch and have not been published to a release.
