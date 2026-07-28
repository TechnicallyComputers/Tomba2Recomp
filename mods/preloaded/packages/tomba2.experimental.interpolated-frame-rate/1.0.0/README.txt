Tomba 2 Temporal Frame Blending (Experimental)

This mod leaves Tomba 2's executable, guest VBlank, simulation, timers, input,
and audio untouched. It blends the two most recent completed display images in
PSXrecomp's OpenGL presentation path.

"Display refresh" follows the measured monitor refresh rate. The
motion-adaptive clarity blend avoids crossfading large pixel changes to reduce
double-image trails. It uses the same zero-sentinel and blend-mode fixes as Ape
Escape. This is temporal blending, not motion-vector frame generation, so it
cannot reconstruct true in-between object positions.
