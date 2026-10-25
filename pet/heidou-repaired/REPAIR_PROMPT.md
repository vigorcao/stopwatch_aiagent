Use $hatch-pet in repair mode on an existing Codex v2 pet. Run end to end without asking questions.

Source pet: ~/.codex/pets/heidou (pet.json + spritesheet.webp, 1536x2288, 8x11 cells of 192x208).
Read it, but do not modify anything in ~/.codex/pets/heidou.

Problem: row 1 (running-right) fails the contract rule "cadence must visibly alternate".
All 8 frames show the same stride: back leg left, front leg right. Only small jitter changes.
On the watch the pet slides across the screen instead of running. Row 2 (running-left) has the same problem.

Repair:
- Regenerate row 1 (running-right) only, as one coherent 8-frame strip.
- Use one full run cycle over 8 frames: contact (left foot forward), down, passing (legs together, crossing),
  up, contact (right foot forward), down, passing, up. The two legs must clearly swap front and back.
- Arms swing opposite to the legs. The body bobs a little: lowest on contact/down, highest on up.
- Keep the pet's identity exactly as in row 0 and the current row 1: same black fluffy body, the same googly eyes,
  open mouth with orange tongue, same black boots, same scale, same baseline, same lighting. Face and travel right.
- No speed lines, dust, shadows, or detached effects.
- Then derive row 2 (running-left) by mirroring the new row 1, if it stays correct when flipped.
- Keep every other row (0 and 3-10) byte-for-byte from the source atlas.

Output:
- Work in the current directory (<run dir>) for the run folder.
- Write the repaired atlas to final/spritesheet.webp and a copy of pet.json to final/pet.json.
- Write a preview GIF of the new running-right row to qa/running-right.gif.
- If load_workspace_dependencies is not available, use PYTHON=<a Python venv with Pillow>.
- When done, print the final folder path and a short QA summary that says whether the legs alternate.
