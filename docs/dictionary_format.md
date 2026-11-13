# Dictionary and Token Format

## Dictionary file

`data/dictionary.txt` contains one entry per line: `decimal_code<TAB>exact_word`.
Codes start at 1, are unique, and fit in `uint32_t`. Words are unique and case-sensitive.
The included mapping is generated from the two supplied input fixtures. Keep the mapping
stable once compressed files have been produced; reassigning codes breaks decoding.

## Tokenization

The reference tokenizer groups ASCII letters, ASCII digits, and underscores into
words. All other bytes are separators and must be preserved verbatim. The
reference inputs contain only ASCII bytes. Preserve case and punctuation exactly.

## Proposed reversible compressed stream (integration contract)

The dictionary library only resolves words and codes; it does **not** serialize
compressed files. For the compressor and decompressor, use the following proposed
byte-level protocol if approved by the team:

- Literal bytes other than `0x1B` are copied as-is.
- Literal `0x1B` is encoded as `0x1B 0x00`.
- A dictionary word is encoded as `0x1B` followed by a base-128 unsigned varint
  containing its positive dictionary code. Varints use seven payload bits per
  byte, with the high bit indicating continuation (LEB128).
- `0x1B` followed by end-of-file, a malformed varint, code zero, or an unknown
  dictionary code is a decoding error.
- The compressor should emit dictionary tokens only if they are shorter than
  the literal word; otherwise write the word unchanged.

The format is a **proposal**, not a format implemented by this library. It must
be agreed with team members A and B before they implement file I/O.

## Integration API

Include `dictionary.h`, link `src/shared/dictionary.c`, and load the same mapping
for compression and decompression. The dictionary must remain alive while the
returned word pointers are used. This module does not implement process IPC,
thread synchronization, or the monitor.
