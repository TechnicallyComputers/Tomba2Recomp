Tomba 2 Interpolated Frame Rate (Experimental)

This mod leaves Tomba 2's executable, guest VBlank, simulation, timers, input,
and audio untouched. It blends the two most recent completed display images in
PSXrecomp's OpenGL presentation path.

"Display refresh" follows the measured monitor refresh rate. It uses the same
zero-sentinel fix as Ape Escape and does not select the old uncapped busy loop.
Interpolation is a presentation-only crossfade and can show blending or
ghosting around fast-moving objects.
