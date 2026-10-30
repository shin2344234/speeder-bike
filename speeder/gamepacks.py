"""Read files out of the game's packs, for pack_assets.py and make_blends.py.

CD Animator's reader (cd_animator/formats/paz.py; CD_ANIMATOR names its folder) unpacks the files,
but its index reader only knows the layout before Steam build 26308950 (exe 1.0.0.3392, 29 October
2026), whose index changed:
- a folder or name tree node starts with a varint, the distance back to its parent node (0 for a
  root), where it had a 4-byte parent offset;
- file names no longer end in their extension: the top byte of a file's flags indexes an
  extension table after the file records, a 4-byte size and then zero-terminated strings, the
  first one empty;
- after the folder records come a 4-byte file count and the 20-byte file records, as before.
This reads either layout (the same logic as patch-dlc's tools/cdpaz.py) and looks in every group,
since a file's group can change between builds too.

    files(game, paths) -> {path (lower case): bytes}
"""
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.environ.get("CD_ANIMATOR", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                             "..", "..", "CD animator")), "cd_animator"))
from formats import paz  # noqa: E402


def _varint(data, off):
    value = shift = 0
    while True:
        b = data[off]
        off += 1
        value |= (b & 0x7F) << shift
        shift += 7
        if b < 0x80:
            return value, off


def _tree(data, off, size):
    """{offset: (parent offset or None, fragment)} for a new-layout fragment tree."""
    nodes = {}
    o, end = off, off + size
    while o < end:
        at = o - off
        delta, o = _varint(data, o)
        n = data[o]
        o += 1
        nodes[at] = (at - delta if delta else None, data[o:o + n].decode("utf-8", "replace"))
        o += n
    if o != end:
        raise ValueError("a fragment tree at %d overruns its size %d" % (off, size))
    cache = {}

    def path(ref):
        if ref in cache:
            return cache[ref]
        if ref not in nodes:
            return ""   # 0xFFFFFFFF: files with no folder
        parts, cur = [], ref
        while cur is not None and cur not in cache:
            parent, text = nodes[cur]
            parts.append(text)
            cur = parent
        cache[ref] = (cache[cur] if cur is not None else "") + "".join(reversed(parts))
        return cache[ref]
    return path


def _new_layout(head):
    """Whether an index uses the 3392 layout, or None when it has no trees to tell by."""
    off = 4
    count = struct.unpack_from("<I", head, off)[0]
    off += 12 + count * 8 + max(count - 1, 0) * 4
    # An old tree's first node starts with a parent of 0xFFFFFFFF, a new one's with a 0 varint.
    for _ in range(2):
        size = struct.unpack_from("<I", head, off)[0]
        if size:
            return head[off + 4:off + 8] != b"\xff\xff\xff\xff"
        off += 4
    return None


def _read_new(pamt, data):
    folder_dir = os.path.dirname(pamt)
    stem = int(os.path.splitext(os.path.basename(pamt))[0])
    off = 4
    count = struct.unpack_from("<I", data, off)[0]
    off += 12 + count * 8 + max(count - 1, 0) * 4
    size = struct.unpack_from("<I", data, off)[0]
    folders = _tree(data, off + 4, size)
    off += 4 + size
    size = struct.unpack_from("<I", data, off)[0]
    names = _tree(data, off + 4, size)
    off += 4 + size
    folder_count = struct.unpack_from("<I", data, off)[0]
    off += 4
    records = [struct.unpack_from("<IIII", data, off + 16 * i) for i in range(folder_count)]
    off += 16 * folder_count
    file_count = struct.unpack_from("<I", data, off)[0]
    off += 4
    files_at = off
    off += 20 * file_count
    size = struct.unpack_from("<I", data, off)[0]
    if off + 4 + size != len(data):
        raise ValueError("%s: the extension table does not end the file" % pamt)
    exts = [e.decode("utf-8") for e in data[off + 4:off + 4 + size].split(b"\0")]
    folder_of = [None] * file_count
    for _hash, ref, first, n in records:
        prefix = folders(ref)
        for i in range(first, min(first + n, file_count)):
            folder_of[i] = prefix
    out = []
    for i in range(file_count):
        node, offset, stored, size, flags = struct.unpack_from("<IIIII", data, files_at + 20 * i)
        name = names(node) + exts[flags >> 24]
        paz_file = os.path.join(folder_dir, "%d.paz" % (stem + (flags & 0xFF)))
        prefix = folder_of[i]
        out.append(paz.Entry("%s/%s" % (prefix, name) if prefix else name, paz_file, offset, stored, size, flags))
    return out


def index(pamt):
    """The entries of one group's index, in either layout."""
    with open(pamt, "rb") as f:
        data = f.read()
    new = _new_layout(data[:4096])
    if new is None:
        return []
    return _read_new(pamt, data) if new else paz.read_index(pamt)


def files(game, paths):
    """{path: bytes} for each of `paths` (compared in lower case), from any group of the game folder.
    A path no group holds is left out."""
    want = {p.lower() for p in paths}
    out = {}
    for pamt in sorted(glob.glob(os.path.join(game, "*", "0.pamt"))):
        for e in index(pamt):
            p = e.path.replace("\\", "/").lower()
            if p in want and p not in out:
                out[p] = paz.read(e)
        if len(out) == len(want):
            break
    return out

