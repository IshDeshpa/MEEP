# 1. define elem IDs and reaction rules
# 2. decode/encode four cell tiles
# 3. eval reactions for each tile
# 4. gen and export lut into c array
import sys
from enum import IntEnum

class Elem(IntEnum):
    AIR = 0
    WATER = 1
    EARTH = 2
    LAVA = 3
    NATURE = 4
    STEAM = 5
    FIRE = 6
    ACID = 7
    ICE = 8
    HOTAIR = 9
    EXPLOSION = 10
    SAND = 11
    BOMB = 12
    BLOCK = 13
    EMITTER = 14
    VOID = 15


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

def chooseEmission(elems):
    counts = {}
    for elem in elems:
        if elem in (Elem.EMITTER, Elem.VOID, Elem.BLOCK, Elem.AIR):
            continue
        counts[elem] = counts.get(elem, 0) + 1
    if not counts:
        return None
    highest = max(counts.values())
    candidates = [elem for elem, count in counts.items() if count == highest]
    return candidates[0]  # choose the first one in case of a tie


# adjacency defintion
# 0 1
# 2 3

# 0's neighbours: 1, 2
# 1's neighbours: 0, 3
# 2's neighbours: 0, 3
# 3's neighbours: 1, 2

neighbours = (
    (1,2), 
    (0,3), 
    (0,3), 
    (1,2)
)

def evalTile(tile):
    original = decodeTile(tile)
    output = original.copy()

    used = [False] * 4

    for i in range(4):
        if used[i]:
            continue

        for j in neighbours[i]:
            if used[j]:
                continue
            result_i = react(original[i], original[j])
            result_j = react(original[j], original[i])

            if result_i != original[i] or result_j != original[j]:
                output[i] = result_i
                output[j] = result_j
                used[i] = True
                used[j] = True
                break

    if Elem.EMITTER in original:
        emission = chooseEmission(original)
        if emission is not None:
            for k in range(4):
                if original[k] == Elem.AIR:
                    output[k] = emission

    return encodeTile(output)

def printTile(tile):
    elems = decodeTile(tile)
    print(f"{int(elems[0])} {int(elems[1])}")
    print(f"{int(elems[2])} {int(elems[3])}")

def generateLUT():
    outputs = []
    for tile in range(0x10000):
        outputs.append(evalTile(tile))
    return outputs

def writeLUT(outputs, filename):
    with open(filename, "wb") as file:
        for output in outputs:
            file.write(output.to_bytes(2, byteorder="little")) # x86 little endian

def readLUT(filename):
    with open(filename, "rb") as file:
        data = file.read()

    if len(data) != 65536 * 2:
        raise ValueError("Expected a 131072-byte LUT file")

    return [
        int.from_bytes(data[i:i + 2], byteorder="little")
        for i in range(0, len(data), 2)
    ]


def main():
    # tile = 0x0031
    # elems = decodeTile(tile)

    # print("Before:")
    # printTile(tile)
    # print(f"Repacked: 0x{encodeTile(elems):04X}")

    # assert encodeTile(elems) == tile
    # assert react(Elem.WATER, Elem.LAVA) == Elem.AIR
    # assert react(Elem.LAVA, Elem.WATER) == Elem.EARTH

    # print(chooseEmission([Elem.EMITTER, Elem.WATER, Elem.WATER, Elem.AIR]))
    # print(chooseEmission([Elem.EMITTER, Elem.WATER, Elem.EARTH, Elem.AIR]))
    # print(chooseEmission([Elem.EMITTER, Elem.AIR, Elem.BLOCK, Elem.VOID]))
    # newTile = evalTile(tile)
    # print(f"Evaluated tile: 0x{newTile:04X}")
    # printTile(newTile)

    outputs = generateLUT()
    # print(f"Generated {len(outputs)} entries")
    # assert outputs[0x0031] == 0x0020

    filename = sys.argv[1] if len(sys.argv) > 1 else "la.bin"
    writeLUT(outputs, filename)

    loaded = readLUT(filename)
    assert loaded == outputs
    print("All LUT entries survived writing and reading.")

    i = 0x1031
    print("Input: ")
    printTile(i)
    print("Output: ")
    printTile(loaded[i])

if __name__ == "__main__":
    main()