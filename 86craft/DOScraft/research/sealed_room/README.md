# Rejected sealed-room draft

This incomplete draft was set aside before build integration or guest testing.
The user correctly prioritizes the existing house with an open doorway: walls
should reject hidden geometry directionally, without requiring a sealed room.
No benchmark fixture was changed, and neither draft file is imported by builders.

Do not promote this code as a safe implementation. The read-only source audit
found additional missing proof obligations: water can suppress published inward
boundary faces even in an otherwise opaque cavity; block opacity does not prove
texture opacity; mesher emission and draw-list overflow can omit needed boundary
faces independently of the trimmed flag. Published meshes, immediate edit
invalidation, near clipping, stream readiness and distance filtering must all
be certified. Its current callback does not meet these obligations.

Retained only so the investigated approach is not permanently lost. No measured
speedup, native safety oracle or playable change is claimed. Future work targets
opaque published wall patches and conservative world-space shadow cones before
terrain projection/subdivision, including the unmodified doorway benchmark.
