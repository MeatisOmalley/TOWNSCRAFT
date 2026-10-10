# Inherited storage issues and separately approved DOS fixes

These are reproduced by `tests/save_file_test.c`, not fixed as part
of the original DOS transport checkpoint. The vendor snapshot and gameplay/cache bodies
remain unchanged. They must be considered during real mixed-source parity tests.

1. **Completed save read versus reused scratch destination.** A completed
   `save_column_read` returns success for the same offset/length/destination
   without recopying. The world uses the same `columnScratch` destination for
   terrain HDD reads and RAM terrain-cache hits. Reading saved A, preparing
   terrain B, then preparing A again can therefore validate B's overwritten
   bytes as A and report a stream error. This behavior exists in Towns `save.c`
   and the original DOS transport preserved it. The native reproduction overwrites the
   shared destination after completion; a real column-cache integration replay
   is still required. **Approved DOS fix:** completion now releases the
   destination. An identical new request recopies from the retained track or
   rereads the necessary tracks; only an in-progress request owns the buffer.
   Regression checks cover both single-track cached recopy and a multi-track
   destination overwritten between completed reads. The vendor stays unchanged.

2. **Old v4 bank can shadow a later v3 save.** Save version selection is retained:
   cached widths below 256 use two-bank v4; the width-256 baseline uses legacy
   single-medium v3. Loading prefers a valid v4 bank before testing legacy track
   zero. If v4 bank 1 remains valid and a new compressed v3 save fits entirely
   inside bank 0, the old v4 snapshot wins, yielding `SAVE_WRONG_SIZE` for width
   256 despite a successful v3 write. The native reproduction uses precisely
   this sequence. Do not force v4, invalidate old banks, change selection, or
   claim legacy saves are transactional without a separate approved fix.

The user approved separate bugfix commits alongside the port. Version precedence
is the next separate fix. Neither issue authorizes a renderer/world optimization.
