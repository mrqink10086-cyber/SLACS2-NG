import argparse
import hashlib
import json
import struct
from pathlib import Path


class Reader:
    def __init__(self, data, offset=0):
        self.data = data
        self.offset = offset

    def read(self, fmt):
        result = struct.unpack_from('<' + fmt, self.data, self.offset)
        self.offset += struct.calcsize('<' + fmt)
        return result[0] if len(result) == 1 else result

    def string(self, size_fmt='I'):
        size = self.read(size_fmt)
        value = self.data[self.offset:self.offset + size]
        if len(value) != size:
            raise ValueError('Truncated string')
        self.offset += size
        return value.rstrip(b'\0').decode('utf-8')


def read_templates(path):
    import lz4.frame

    data = path.read_bytes()
    magic, version, folder_start, flags, folder_count, file_count, _, name_bytes, _ = struct.unpack_from('<4s8I', data)
    if magic != b'BSA\0' or version != 105:
        raise ValueError('This probe supports SSE BSA version 105 only')
    records = []
    name_start = 0
    for folder in range(folder_count):
        _, count, _, offset = struct.unpack_from('<QIIQ', data, folder_start + folder * 24)
        r = Reader(data, offset - name_bytes)
        folder_name = r.string('B')
        for _ in range(count):
            _, packed_size, file_offset = r.read('QII')
            records.append((folder_name, packed_size, file_offset))
        name_start = max(name_start, r.offset)
    if len(records) != file_count:
        raise ValueError('BSA file-count mismatch')
    name_offset = name_start
    result = []
    for folder, packed_size, offset in records:
        end = data.index(b'\0', name_offset)
        name = data[name_offset:end].decode('utf-8')
        name_offset = end + 1
        if name.lower() not in {'face_overlay.nif', 'body_overlay.nif', 'hands_overlay.nif', 'feet_overlay.nif'}:
            continue
        size = packed_size & 0x3fffffff
        compressed = bool(flags & 4) != bool(packed_size & 0x40000000)
        payload = data[offset:offset + size]
        if flags & 0x100:
            payload = payload[1 + payload[0]:]
        if compressed:
            expected = struct.unpack_from('<I', payload)[0]
            payload = lz4.frame.decompress(payload[4:])
            if len(payload) != expected:
                raise ValueError('BSA decompressed-size mismatch')
        result.append((folder + '\\' + name, payload))
    if len(result) != 4:
        raise ValueError(f'Expected four normal overlay templates, found {len(result)}')
    return result


def inspect_nif(data):
    r = Reader(data, data.index(b'\n') + 1)
    version, endian, user, count, bs_version = r.read('IBIII')
    if (version, endian, user, bs_version) != (0x14020007, 1, 12, 83):
        raise ValueError('This probe supports Skyrim 20.2.0.7/user12/BS83 templates only')
    for _ in range(3):
        r.string('B')
    types = [r.string() for _ in range(r.read('H'))]
    indices = [r.read('H') for _ in range(count)]
    sizes = [r.read('I') for _ in range(count)]
    strings_count = r.read('I')
    r.read('I')
    strings = [r.string() for _ in range(strings_count)]
    for _ in range(r.read('I')):
        r.read('I')
    result = {'version': '20.2.0.7', 'userVersion': user, 'bsVersion': bs_version, 'strings': strings, 'blocks': []}
    for index, (type_index, size) in enumerate(zip(indices, sizes)):
        raw = data[r.offset:r.offset + size]
        kind = types[type_index]
        block = {'index': index, 'type': kind, 'offset': r.offset, 'size': size}
        if kind == 'BSLightingShaderProperty':
            shader, _, extra_count, _, flags1, flags2 = struct.unpack_from('<IiIiII', raw)
            if extra_count != 0 or size != 112:
                raise ValueError('Unexpected shader layout')
            block.update(shaderType=shader, flags1=f'0x{flags1:08x}', flags2=f'0x{flags2:08x}',
                         alpha=struct.unpack_from('<f', raw, 64)[0],
                         zTest=bool(flags1 & (1 << 31)), zWrite=bool(flags2 & 1),
                         decal=bool(flags1 & (1 << 26)), facegenDetailMap=bool(flags1 & (1 << 10)))
        elif kind == 'NiAlphaProperty':
            if size != 15:
                raise ValueError('Unexpected alpha-property layout')
            flags, threshold = struct.unpack_from('<HB', raw, 12)
            block.update(flags=flags, threshold=threshold, blend=bool(flags & 1),
                         sourceBlend=(flags >> 1) & 15, destinationBlend=(flags >> 5) & 15,
                         alphaTest=bool(flags & 512), testFunction=(flags >> 10) & 7,
                         noSorter=bool(flags & 8192))
        elif kind == 'NiTriShape':
            block.update(shaderProperty=struct.unpack_from('<i', raw, len(raw) - 8)[0],
                         alphaProperty=struct.unpack_from('<i', raw, len(raw) - 4)[0])
        elif kind == 'BSShaderTextureSet':
            texture_reader = Reader(raw)
            block['textures'] = [texture_reader.string() for _ in range(texture_reader.read('I'))]
            if texture_reader.offset != size:
                raise ValueError('Unexpected texture-set layout')
        result['blocks'].append(block)
        r.offset += size
    if r.offset + 8 != len(data):
        raise ValueError('Unexpected NIF footer')
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('bsa', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = {'archive': str(args.bsa.resolve()), 'archiveSha256': hashlib.sha256(args.bsa.read_bytes()).hexdigest(), 'templates': []}
    for archive_path, data in read_templates(args.bsa):
        result['templates'].append({'archivePath': archive_path, 'sha256': hashlib.sha256(data).hexdigest(),
                                    'bytes': len(data), **inspect_nif(data)})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Inspected {len(result["templates"])} templates -> {args.output}')


if __name__ == '__main__':
    main()
