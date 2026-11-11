"""A gdb pretty-printer for the game's wrestler records (Wrestler, the decompilation's include/wrestler.h).

gdb then shows a record as one line, with every field still there to expand below it:

    wrestler 1: char 0x201, state 46 tied up other, step 2, anim 0E70 frame 12, health 243/255, spirit 49,
    at (85, 90, -60), heading 176 deg, held by 0, opponent 0

VS Code's launch entries for Turnbuckle Dive load it (.vscode/launch.json); in gdb in a terminal, type
`source tools/gdb/wrestler.py` from this repository's folder. The state names come from the enum TdWrState, which the
debug information of src/dev/dev_records.c holds (src/dev/wr_states.h), so they always match the program.
"""
import gdb
import gdb.printing


def _state_names():
    try:
        enum = gdb.lookup_type('enum TdWrState')
    except gdb.error:
        return {}
    return {int(f.enumval): f.name[len('TD_WR_'):].lower().replace('_', ' ') for f in enum.fields()}


class WrestlerPrinter:
    names = None

    def __init__(self, val):
        self.val = val

    def to_string(self):
        v = self.val
        if WrestlerPrinter.names is None:
            WrestlerPrinter.names = _state_names()
        if int(v['characterId']) == -1:
            return 'wrestler %d: empty' % int(v['id'])
        state = int(v['state'])
        held = int(v['heldById'])
        return ('wrestler %d: char 0x%03X, state %02X %s, step %d, anim %04X frame %d, health %d/%d, spirit %d, '
                'at (%.0f, %.0f, %.0f), heading %.0f deg, held by %s, opponent %d'
                % (int(v['id']), int(v['characterId']) & 0xFFFF, state, WrestlerPrinter.names.get(state, ''),
                   int(v['step']), int(v['animId']) & 0xFFFF, int(v['animFrame']), int(v['health']),
                   int(v['healthMax']), int(v['spirit']), float(v['x']), float(v['y']), float(v['z']),
                   int(v['heading']) * 360.0 / 4096.0, '-' if held < 0 else str(held), int(v['opponentId'])))

    def children(self):
        for field in self.val.type.strip_typedefs().fields():
            yield field.name, self.val[field.name]


def _build():
    printer = gdb.printing.RegexpCollectionPrettyPrinter('turnbuckle-dive')
    printer.add_printer('Wrestler', '^Wrestler$', WrestlerPrinter)
    return printer


gdb.printing.register_pretty_printer(gdb.current_objfile(), _build(), replace=True)
