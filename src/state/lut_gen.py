# 1. define elem IDs and reaction rules
# 2. decode/encode four cell tiles
# 3. eval reactions for each tile
# 4. gen and export lut into c array

from enum import IntEnum

class Elem(IntEnum):
    AIR = 0
    WATER = 1
    EARTH = 2
    LAVA = 3
    BLOCK = 4
    EMITTER = 5
    VOID = 6

# note: block does nothing, emitter spawns stuff in its tile, void replaces all in its tile with air
# rules[source][neighbour] = result

rules = {
    Elem.WATER: {
        Elem.LAVA: Elem.AIR
    },
    Elem.LAVA: {
        Elem.WATER: Elem.EARTH
    }
}

def react(elem, neighbour):
    element_rules = rules.get(elem, {})
    return element_rules.get(neighbour, elem) # the result from looking at the element's dictionary of rules

def decodeTile(tile):
    return [
        Elem((tile >> 12) & 0xF),
        Elem((tile >> 8) & 0xF),
        Elem((tile >> 4) & 0xF),
        Elem(tile & 0xF)
    ]