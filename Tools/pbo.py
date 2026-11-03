"""Minimal PBO reader for the map textures (uncompressed entries only, which is how BI ships textures).

Usage: python -I pbo.py list <file.pbo> [substring]
       python -I pbo.py extract <file.pbo> <entry path> <out file>
"""
import struct
import sys


def read_entries(path):
    with open(path, "rb") as f:
        data = f.read()
    pos, entries, prefix = 0, [], ""

    def cstr():
        nonlocal pos
        end = data.index(b"\0", pos)
        s = data[pos:end].decode("latin-1")
        pos = end + 1
        return s

    while True:
        name = cstr()
        method, size, _, _, datasize = struct.unpack_from("<5I", data, pos)
        pos += 20
        if name == "" and method == 0x56657273:      # "Vers" header: key/value pairs until an empty key
            while True:
                key = cstr()
                if not key:
                    break
                value = cstr()
                if key == "prefix":
                    prefix = value
            continue
        if name == "":
            break
        entries.append((name, method, size, datasize))
    out, offset = [], pos
    for name, method, size, datasize in entries:
        out.append((name, method, size, datasize, offset))
        offset += datasize
    return data, prefix, out


def main():
    cmd, path = sys.argv[1], sys.argv[2]
    data, prefix, entries = read_entries(path)
    if cmd == "list":
        sub = sys.argv[3].lower() if len(sys.argv) > 3 else ""
        print("prefix", prefix)
        for name, method, size, datasize, _ in entries:
            if sub in name.lower():
                print(name, datasize, "packed" if method else "")
    elif cmd == "extract":
        want = sys.argv[3].lower().replace("/", "\\")
        for name, method, size, datasize, offset in entries:
            if name.lower() == want:
                if method:
                    raise SystemExit("compressed entry, not supported")
                with open(sys.argv[4], "wb") as f:
                    f.write(data[offset:offset + datasize])
                print("wrote", sys.argv[4], datasize)
                return
        raise SystemExit("not found: " + want)


if __name__ == "__main__":
    main()
