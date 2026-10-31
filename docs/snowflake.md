# Snowflake deployment and operational notes

## Configuration

Snowflake configuration is captured once per linked library instance, on that instance's first `Snowflake::Generate()` call. A process that links Common once therefore reads it once; independently linked copies (for example two DLLs that each contain a static copy) each read it on their own first call. Set `BEDROCK_COMMON_STATE_DIR` and `BEDROCK_SNOWFLAKE_MACHINE_ID` before any instance in the process makes its first call, and do not change them afterwards: every instance must observe the same values. Both a successful configuration and a configuration failure are cached for the lifetime of the instance; a failed first call is not retried by later calls.

The state directory must be a provisioned, absolute local directory writable by every participating service account. If the override is absent, Windows uses the ProgramData known folder plus `Bedrock\Common`; Linux uses `/var/lib/bedrock-common`. No directory or machine ID is provisioned automatically. A missing, relative, non-directory or inaccessible state directory, and on Windows any path that is not drive-letter absolute (including UNC paths), is reported as `kInvalidStatePath` (-3). A directory on a network or otherwise unsupported volume is reported as `kUnsupportedPlatform` (-16): on Windows only fixed or removable drives are accepted, checked both for the given drive letter and after junctions and symbolic links are resolved; on Linux, known network and cluster filesystems (NFS, CIFS/SMB, Ceph and similar) are rejected.

A present machine-ID environment value takes precedence over `<state-dir>/machine-id`; it must contain exactly one decimal digit from 0 through 7, with optional surrounding ASCII whitespace. An invalid present value is an error rather than a reason to fall back to the file.

## Shared registry and uniqueness scope

All processes, containers on the same server, and independently linked library copies must use the same state directory and the same configured machine ID. The registry is `snowflake.registry`; `snowflake.registry.lock` serializes registration (registry creation or validation and slot selection), and a registering thread waits for it. The registry contains fixed metadata and 128 kernel-locked issuer slots, not generated IDs or timestamps. Do not remove, replace, or edit either registry file while participants or retained IDs exist. Do not change the machine ID or split participants across different state directories.

Issuer state is per thread within a linked library instance: each issuing thread holds one server-wide slot through its own open registry handle until the thread ends. Busy slots are skipped without waiting; when all 128 are held, generation returns `kSlotsExhausted` (-11). A failure of the registration-gate lock or of a slot lock that is not merely busy returns `kLockFailed` (-10), or `kUnsupportedPlatform` (-16) when the platform has no supported lock primitive. Registry open, read and write failures return `kRegistryIoFailed` (-6); an open rejected as an invalid path returns `kInvalidStatePath` (-3), and an unsupported platform `kUnsupportedPlatform` (-16).

The implementation assumes the host clock is globally nondecreasing across issuer handoff. Slot acquisition waits until the clock advances past the acquisition sample. A frozen clock can therefore delay first issuance without bound; this is not a lock timeout or lease. At most 128 issuing threads may hold slots concurrently across processes and library copies. At one millisecond, an issuer can emit 4096 IDs; further issuance in the same millisecond immediately returns `kSequenceExhausted` (-15) without changing issuer state, for caller retry, rather than waiting, spinning or fabricating a future timestamp. Zero is a valid identity; only negative results are errors.

A creator terminated partway through initial registry creation leaves corrupt state and generation fails closed. Recovery is an offline administrative operation: stop all participants, preserve/inspect the files and retained IDs, then provision a safe replacement only under an operationally chosen uniqueness policy. The library never resets metadata automatically. Clock rollback observed by an active issuer is reported. Uniqueness is not guaranteed across clock rollback after issuer loss/restart, VM snapshots/clones, registry deletion/replacement, or machine-ID reassignment.

On Linux, post-fork use in the child is unsupported until `exec`; open-file-description locks are inherited by `fork`. The Linux backend and non-Windows behavior must be validated on the actual target OS/filesystem before deployment. Network/remote filesystems are unsupported. Unload a library only after its issuing threads and TLS state have ended.

## Test coverage and verification limits

`common_internal_tests` covers the generator and its OS backend:

- `SnowflakeCore`: pure issuance arithmetic (zero as a valid identity, timestamp and sequence bounds, immediate sequence exhaustion with unchanged state and recovery on the next tick, rollback high-water handling, the acquisition fence and a retry after a failed fence) and the lock-failure mapping used by both registration lock paths.
- `OsIo`: local-directory validation (relative, missing and regular-file inputs and an accepted local directory on every platform; UNC, drive-relative and invalid UTF-8 inputs on Windows), environment reads, independent-open lock contention, waiting locks, lock ownership transfer and slot locks beyond the registry end of file. Every file lives in a directory created exclusively for one test.
- The Snowflake process suites run fresh child processes for configuration, first-call caching, registry validation, slot capacity and owner-termination recovery. The static-copy suite (Windows only) checks two independently linked library copies.

`common_external_tests` covers the public API and the Archive status boundary.

This section describes intended coverage; it is not execution evidence. Results come from the CTest/JUnit output of a specific build. Windows runs do not verify the Linux backend (open-file-description locking and filesystem-type rejection), which must be run on the Linux target. The automated suites do not create network drives, mounts or shares, so rejection of a live network volume is checked only where such a volume already exists, outside the suites.
