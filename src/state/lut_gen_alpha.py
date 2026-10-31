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
    BLOCK = 4
    EMITTER = 5
    VOID = 6

unused = set()

properties = {
    Elem.AIR:     {"density": 0, "movable": True},
    Elem.WATER:   {"density": 1, "movable": True},
    Elem.LAVA:    {"density": 2, "movable": True},
    Elem.EARTH:   {"density": 3, "movable": True},
    Elem.BLOCK:   {"density": 0, "movable": False},
    Elem.EMITTER: {"density": 0, "movable": False},
    Elem.VOID:    {"density": 0, "movable": False},
}

debug = (Elem.EMITTER, Elem.VOID, Elem.EMITTER)

# all non-debug elements should fall downwards in gravity, maybe later fire can fizzle upwards
# TODO: figure out if reactions and movements should happen together? i think so but need to confirm

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


def applyGravity(elems):
    outputs = elems.copy()
    moved = [False] * 4
    
    for top, bottom in ((0,2), (1,3)):
        top_p = properties[outputs[top]]
        bot_p = properties[outputs[bottom]]

        if top_p["density"] > bot_p["density"]:
            outputs[top], outputs[bottom] = outputs[bottom], outputs[top]
            moved[top] = True
            moved[bottom] = True       

    return outputs, moved




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
    moved = [False] * 4
    g_original, moved = applyGravity(original)
    output = g_original.copy()
    used = moved.copy()

    for i in range(4):
        if used[i]:
            continue

        for j in neighbours[i]:
            if used[j]:
                continue
            result_i = react(g_original[i], g_original[j])
            result_j = react(g_original[j], g_original[i])

            if result_i != g_original[i] or result_j != g_original[j]:
                output[i] = result_i
                output[j] = result_j
                used[i] = True
                used[j] = True
                break

    if Elem.EMITTER in g_original:
        emission = chooseEmission(g_original)
        if emission is not None:
            for k in range(4):
                if g_original[k] == Elem.AIR:
                    output[k] = emission

    if Elem.VOID in g_original:
        for k in range(4):
            if g_original[k] not in (Elem.AIR, Elem.BLOCK, Elem.VOID):
                output[k] = Elem.AIR

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

    i = 0x1300
    print("Input (tick 0): ")
    printTile(i)
    print("Intermediate (tick 1): ")
    printTile(loaded[i])
    j = loaded[i]
    print("Next Outpit (tick 2): ")
    printTile(loaded[j])


if __name__ == "__main__":
    main()