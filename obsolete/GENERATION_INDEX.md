# XNODE Firmware Generation Archive

Workspace root:
- `C:\GitHub\XNODE`

Purpose:
- Keep each working firmware generation in a separate full filebase.
- Preserve rollback points without mixing experimental changes into older snapshots.
- Make later migration into git cleaner and less ambiguous.

## Generation layout

## Active repo

Current git-tracked project:
- `C:\GitHub\XNODE`

Role:
- Canonical active firmware repo for LilyGO Watch Gen3 / XNODE work.
- New changes, builds, and flashes should be run from here.

Archive root:
- `C:\GitHub\XNODE\obsolete\backup`

## Archived generations

### Gen1
Path:
- `C:\GitHub\XNODE\obsolete\backup\My-TTGO-Watch-Gen1`

Role:
- Frozen baseline of the original UI/firmware that was successfully flashed and recovered to a working state.

Rule:
- Do not edit.

### Gen2
Path:
- `C:\GitHub\XNODE\obsolete\backup\My-TTGO-Watch-Gen2`

Role:
- Meshtastic integration branch based on Gen1.
- Includes text messaging, position report decoding, and OSM marker plumbing.

State:
- Frozen reference snapshot.
- Boots and sends messages.
- Screen timeout / dark wake behavior is not finished.

Rule:
- Do not continue feature work here.

### Gen3
Path:
- `C:\GitHub\XNODE\obsolete\backup\My-TTGO-Watch-Gen3`

Role:
- Final non-git working copy before migration into `C:\GitHub\XNODE`.
- Preserved as an archive snapshot of the working Gen3 codebase.

Rule:
- Do not continue active development here.

## Supporting repos

Reference repos currently present locally:
- Archived snapshots now live under `C:\GitHub\XNODE\obsolete\backup`
- Required T-Watch S3 support libraries are vendored in `C:\GitHub\XNODE\support\twatch-s3-libdeps`

## Migration note

The active Gen3 code was moved into the git-tracked `C:\GitHub\XNODE` repo.

This archive was moved into `C:\GitHub\XNODE` so the old `lilygo` workspace can be deleted without losing the generation snapshots.
