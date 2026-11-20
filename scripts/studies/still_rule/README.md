# The still rule on natural pictures

`include/veyra/pipeline/PushHudStill.h` keeps a generated frame's pixel as the real frame had it when the pixel is
"the same in both real frames". This study asks how often that is true of pixels that are **moving**.

It draws pictures with the statistics of natural images (1/f noise) at several contrasts, pans them by 6, 16 and
40 pixels a frame with and without a 30 FPS game's motion blur, and runs the rule's own header on the result
(the rule as 4.0 shipped it and as the header has it now). Nothing in these pictures stands still, so every pixel
the rule keeps is a mistake.

```
cd scripts/studies/still_rule
g++ -O2 -std=c++20 -I../../../include sim.cpp -o sim
python3 study.py          # needs numpy
```

Result of 20 November 2026: 4.0's rule overwrote 5 to 38 % of the generated frame at contrasts of 6 to 12 steps of
255 (each kept pixel wrong by 2 to 9 steps), and the island version (a quarter, reach by height) still 5 to 19 % at
a contrast of 12. That is why the rule is off unless switched on since 4.0.1. The drawn pictures it was first
measured on (`ghost_report` in `scripts/pushflow/prepare_runtime.py`) had hard edges and almost no chance matches.

Not part of any build or workflow.
