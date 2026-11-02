# SDFS metadata transactions and whole-file replacement

## Replacement protocol

`sdfs_write_file()` now replaces an existing file using copy-on-write. It
builds a new data chain without changing the old directory entry, checks every
block write, flushes the new contents, persists the allocation bitmap, and
then journals one complete directory block containing the replacement entry.
Only after that metadata transaction is durable does it release the old data
chain.

The WAL transaction is ordered as follows:

1. Write each journal entry's full-block payload and entry header; flush them.
2. Write the journal header with `dirty=1` and the committed entry count; flush.
   This header is the transaction commit record.
3. Apply the target directory block and flush it.
4. Clear the journal header and flush it.

Mount validates the journal header and each entry before replay. Replay checks
the target range and payload CRC, applies every committed snapshot, flushes the
target writes, then clears the journal. If any validation or write fails, the
journal stays dirty and mount refuses the filesystem. The normal boot path
recognizes this as filesystem damage and uses a RAM disk rather than formatting
the existing image.

The existing 16-block journal holds at most seven complete block snapshots.
Whole-file replacement currently needs one snapshot because its directory
entry is kept in one directory block. The journal is not a general filesystem
transaction manager for arbitrary sequences.

## Failure outcomes

- Before the commit record is durable, the old file remains reachable. Newly
  allocated blocks can be leaked if an I/O failure makes commit state
  uncertain.
- After commit, remount replays the new directory block. A reported write
  failure can therefore mean the operation's final state is pending recovery.
- The old chain is released only after the new directory entry is committed.
  Interruption during cleanup can leak old blocks, but cannot make the new file
  point at freed blocks.
- A corrupt committed journal is retained and rejected; it is not cleared or
  skipped to force a mount.
- `sdfs_delete()` now removes the directory entry before releasing its data
  blocks, so an interruption can leak space instead of leaving a live name for
  blocks already returned to the allocator.
- Open SDFS nodes are tracked by inode/start block. Unlink removes the name but
  defers freeing a file's chain until the final close; an open descriptor keeps
  reading the original bytes. An open directory similarly keeps its block
  allocated after empty-directory removal. This is a runtime lifetime rule,
  not a multi-operation transaction.

There is no SDFS garbage collector or bitmap reconstruction yet. Leaked blocks
remain allocated. The block-level fault injection covers returned read/write
errors and remount replay; it is not a physical power-cut simulator.

## Limits

This guarantee applies to the kernel's whole-file `sdfs_write_file()` path and
single-block metadata writes through the journal. It does not make arbitrary
in-place VFS writes, `rename()`, or multiple filesystem calls atomic. Rename
removes the source entry and adds the destination in separate metadata writes;
it attempts an in-memory rollback if the addition fails, but a power loss
between writes can leave the source name missing. Concurrent filesystem
mutations are not yet covered by a global filesystem lock. Device flush
completion has been implemented for the AHCI path and exercised in QEMU;
real-device power-loss behavior still depends on the controller and drive
honoring FLUSH CACHE EXT.

The offline host installer in [SDFS_INSTALL.md](SDFS_INSTALL.md) edits a
temporary image copy and publishes the complete file by host rename. It does
not invoke the kernel journal and has its own documented limits.

## Tests

The existing 49-case kernel manifest remains unchanged. `sdfs_overwrite`
injects a failure at the journal commit block and at the target directory
block, remounts, and checks that it sees the complete old or new contents.
`journal_multiple_entries` corrupts a committed journal payload, verifies
replay and mount refuse it without clearing the record, restores the journal,
and verifies successful replay. The normal `make app-install-test` path also
boots a real temporary AHCI disk, exercising write/flush ordering outside the
test RAM disk.
