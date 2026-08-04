Tomba 2 First-Person Camera (Experimental)

This limited experiment reuses the stock game's 3D renderer and camera-matrix
builder. It does not replace geometry, alter the disc image, or change save
data.

Controls while the first-person camera owns normal gameplay:

  Select       Enter first-person immediately, including during interactions
               such as holding a pig. Leaving waits until Tomba's physical
               position is stable so moving platforms are not interrupted.
  Up           Walk forward in the direction the camera faces
  Down         Smoothly turn around, then walk forward the opposite way
  Left / Right Rotate the camera only; Tomba does not move
  L1 / R1      Send Tomba's original Up / Down inputs for doors, ladders,
               and path-depth transitions

The mod starts in the stock third-person view each launch. Tomba still moves on
the original authored 2.5D paths, so camera-relative intent is quantized to the
nearer native path direction. Scripted cameras and transitions take priority
when the game requests them. Face-button actions stay game-owned. Their
side/depth directions pass through unchanged, while Jump+Up keeps the action
button and translates Up to first-person forward movement so pig capture stays
on Tomba's normal gameplay path. Expect some model clipping and rooms whose
scenery was never authored to be seen from this angle.
