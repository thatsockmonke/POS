POS 0.6.6-dev — focused updater heap fix, based on 0.6.4-dev.

- Fix "Not enough heap for manifest" caused by fixed 32 KB text buffers and
  64-file package allocations, multiplied by nested history verification.
- Allocate actual manifest bytes and file-record count, close file buffers before
  parsing, and release redundant JSON/discovery metadata before later phases.
- Retain all 64 records, the 32 KB input limit, signatures, hashes, transaction
  ordering, factory recovery and both OTA slots. Add allocation-pressure/failure
  regressions and update-stage USB heap logging.
- This bridge deliberately keeps 0.6.4's app/UI behavior. The numeric microphone
  gain and world map belong to the following full release.

Important: an installed updater that cannot allocate its manifest cannot repair
itself merely by downloading a smaller ZIP. A fresh-boot retry may help with
fragmentation; otherwise use the file-preserving USB/Backup & Reinstall guide in
docs/UPDATER_HEAP.md (included in the ZIP). Do not format the microSD.

Validation: 370 host tests pass (365 core/regression checks plus 5 release-hold
checks), and both pinned ESP-IDF roles build. Constrained tests cover 12 KB POS/cJSON manifest parsing, 32 KB
history+installation with target metadata held, 64-file/32 KB limits, and every
manifest allocation failure without leaked owned buffers. Crypto/libc internals
are outside this synthetic budget. No physical Cardputer tests are claimed.

The larger feature/provider release is held until the user explicitly confirms
installing this bridge. This release remains newest during that staged rollout.
