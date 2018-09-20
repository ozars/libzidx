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

Checkpoint metadata is separated from checkpoint window data to allow
sequential reading of metadata and lazy reading of window data, in case it is
implemented in the future.

## Header

- 4 bytes: ASCII "ZIDX" string (hex "5a 49 44 58").
- 2 bytes: File format version (currently hex "00 00").
- 1 byte: Type of checksum algorithm used for the uncompressed file.
    - None `0x1`.
    - CRC-32 `0x2`.
    - Adler-32 `0x3`.
- 1 byte: Type of indexed file.
    - GZIP `0x1`.
    - Raw DEFLATE `0x2`.
    - ZLIB `0x3`.
- 4 bytes: Number of checkpoints.
- 4 bytes: Checksum value. Zero if no checksum is used.
- 8 bytes: Length of compressed indexed file, `-1` if unknown.
- 8 bytes: Length of uncompreesed indexed file, `-1` if unknown.

## Checkpoint Metadata Section
- For every checkpoint:
    - 8 bytes: Uncompressed offset
    - 8 bytes: Compressed offset
    - 1 byte: Number of bits used from next compressed byte on block boundary
    - 1 byte: Compressed byte on block boundary if there is one, else zero
    - 4 bytes: Length of the window
    - 4 bytes: Checksum of the uncompressed data upto checkpoint offset. Zero if
    no checksum is used.

