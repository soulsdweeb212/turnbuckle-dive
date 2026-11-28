Test data for the test game_calls_detects_crash (include/types.h says what this folder is for), not a document:
tools/port_calls.py reads the Nintendo SDK functions from this table, and needs one row. Nothing calls the one it lists.

## The functions

| Address | Function | What it is | Library |
|---|---|---|---|
| 80002000 | `osUnused` | not called | libultra |
