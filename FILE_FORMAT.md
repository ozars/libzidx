# zidx File Format
*Version: 1.0*

This document describes file format used for importing and exporting indexes for
compressed GZIP, ZLIB or DEFLATE data.

Conventionally, *.zx* or *.zidx* file extensions are used for storing checkpoint
indexing data created by zidx library.

Byte order is little-endian for all fields by default.

## General Structure of File

- Header
- Checkpoint Metadata Section
- Checkpoint Window Data

Checkpoint metadata is separated from checkpoint window data, to allow
sequential reading of metadata and lazy reading of window data, if it is
implemented in the future.

## Header

- 4 bytes: ASCII "ZIDX" string (hex "5a 49 44 58").
- 2 bytes: File format version (currently hex "00 00").
- 2 bytes: Type of checksum algorithm used in this file.
    - None `0x0`: If no checksum is used at all in this file.
    - CRC-32 `0x1`.
    - Adler-32 `0x2`.
- 4 bytes: Checksum of the rest of header, zero if none used. Extra header
  length is included while computing the checksum if there exists one, but extra
  header data is not included even if there exists some.
- 2 bytes: Type of indexed file.
    - GZIP `0x1`.
    - Raw DEFLATE `0x2`.
    - ZLIB `0x3`.
- 8 bytes: Length of compressed indexed file, 0 if unknown.
- 8 bytes: Length of uncompreesed indexed file, 0 if unknown.
- 4 bytes: Checksum of indexed file, 0 if `ZX_UNKNOWN_CHECKSUM` is set.
- 4 bytes: Number of indexed checkpoints in checkpoint metadata.
- 4 bytes: Checksum of whole checkpoint metadata. Different from header
  checksum, this includes both extra data length and extra data content in each
  checkpoint if there exists some.
- 4 bytes: Flags
    - ZX_EXTRA_HEADER `0x1`: Indicates there are extra space coming after
    header.
    - ZX_EXTRA_METADATA `0x2`: Indicates there are extra space coming after
    each checkpoint metadata.
    - ZX_UNKNOWN_CHECKSUM `0x4`: Checksum of whole indexed file is not known for
      some reason.
    - ZX_UNKNOWN_WINDOW_CHECKSUM `0x8`: Checksum of window in each checkpoint
    metadata do not exist for some reason.
    - Rest of bits in flags zero by default if their meaning is not known.
- [Optional] 8 bytes: Length of extra data coming after header. This field does
  not exist if `ZX_EXTRA_HEADER` is not passed in flags.

## Checkpoint Metadata Section
- 8 bytes: Uncompressed offset
- 8 bytes: Compressed offset
- 1 byte: Number of bits used from next compressed byte on block boundary
- 1 byte: Compressed byte on block boundary if there is one, else zero
- 8 bytes: Absolute offset of window data.
- 4 bytes: Length of the window
- [Optional] 4 bytes: Checksum of the window, this field does not exist if the
  flag in the header passes `ZX_UNKNOWN_WINDOW_CHECKSUM`.
- [Optional] 8 bytes: Length of extra space used after this metadata, if the
  flag in the header passes `ZX_EXTRA_METADATA` option.

