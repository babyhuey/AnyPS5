import struct
import sys

ENCRYPTED = 1 << 1
COMPRESSED = 1 << 3
BLOCKED = 1 << 11


def unwrap(data):
    count, = struct.unpack_from('<H', data, 0x18)
    entries = [struct.unpack_from('<QQQQ', data, 0x20 + i * 0x20) for i in range(count)]
    elf = 0x20 + count * 0x20
    if data[elf:elf + 4] != b'\x7fELF':
        raise ValueError('no embedded ELF header')
    phoff, = struct.unpack_from('<Q', data, elf + 0x20)
    phentsize, phnum = struct.unpack_from('<HH', data, elf + 0x36)
    headers = data[elf:elf + phoff + phentsize * phnum]
    phdrs = [struct.unpack_from('<IIQQQQQQ', headers, phoff + i * phentsize) for i in range(phnum)]
    size = max([len(headers)] + [p[2] + p[5] for p in phdrs])
    out = bytearray(size)
    out[:len(headers)] = headers
    struct.pack_into('<QHH', out, 0x28, 0, 0, 0)
    struct.pack_into('<H', out, 0x3e, 0)
    copied = 0
    for props, offset, filesz, _ in entries:
        if not props & BLOCKED:
            continue
        if props & (ENCRYPTED | COMPRESSED):
            raise ValueError('encrypted or compressed segment')
        index = (props >> 20) & 0xfff
        if index >= phnum or phdrs[index][5] != filesz:
            raise ValueError(f'segment {index} does not match its program header')
        out[phdrs[index][2]:phdrs[index][2] + filesz] = data[offset:offset + filesz]
        copied += 1
    return bytes(out), copied, phnum


if __name__ == '__main__':
    blob = open(sys.argv[1], 'rb').read()
    result, copied, phnum = unwrap(blob)
    open(sys.argv[2], 'wb').write(result)
    print(f'{copied} segments copied, {phnum} program headers, {len(result)} bytes')
