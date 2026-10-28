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

unused = set()

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

def toElem(value):
    try:
        return Elem(value)
    except ValueError:
        if value not in unused:
            print(f"Unused element ID: {value}")
            unused.add(value)

        return value

def react(elem, neighbour):
    element_rules = rules.get(elem, {})
    return element_rules.get(neighbour, elem) # the result from looking at the element's dictionary of rules

def decodeTile(tile):
    return [
        toElem((tile >> 12) & 0xF),
        toElem((tile >> 8) & 0xF),
        toElem((tile >> 4) & 0xF),
        toElem(tile & 0xF)
    ]

def encodeTile(elems):
    return (elems[0] << 12) | (elems[1] << 8) | (elems[2] << 4) | elems[3]

# adjacency defintion
# 0 1
# 2 3

# 0's neighbours: 1, 2
# 1's neighbours: 0, 3
# 2's neighbours: 0, 3
# 3's neighbours: 1, 2

def main():
    tile = 0x0031
    elems = decodeTile(tile)

    print(elems)
    print(f"Repacked: 0x{encodeTile(elems):04X}")

    assert encodeTile(elems) == tile
    assert react(Elem.WATER, Elem.LAVA) == Elem.AIR
    assert react(Elem.LAVA, Elem.WATER) == Elem.EARTH

if __name__ == "__main__":
    main()