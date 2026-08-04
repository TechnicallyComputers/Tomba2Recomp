Tomba 2 First-Person Camera (Experimental)

This limited experiment reuses the stock game's 3D renderer and camera-matrix
builder. It does not replace geometry, alter the disc image, or change save
data.

Controls while the first-person camera owns normal gameplay:

  Select       Enter first-person immediately, including during interactions
               such as holding a pig. Press again to leave immediately.
  Left stick up
               Walk forward in the direction the camera faces
  Left stick down
               Send one stock opposite-direction turn input, then remain
               neutral while held
  Left stick left / right
               No movement
  Right stick  Free-look horizontally and vertically
  L1 / R1      Send Tomba's original Up / Down inputs for doors, ladders,
               and path-depth transitions

The mod starts in the stock third-person view each launch. Tomba's third-person
camera continues running invisibly for gameplay, spawning, and culling; the mod
replaces only the view matrix used when drawing the scene. Tomba still moves on
the original authored 2.5D paths, so camera-relative intent is quantized to the
nearer native path direction. Scripted cameras and transitions take priority
when the game requests them. Face-button actions stay game-owned. Their
side/depth directions pass through unchanged, while Jump+forward keeps the
action button and translates movement to Tomba's native path so pig capture
stays on the normal gameplay path. Expect some model clipping and rooms whose
scenery was never authored to be seen from this angle.
