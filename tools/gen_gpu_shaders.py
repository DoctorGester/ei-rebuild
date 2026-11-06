"""The HLSL programs' code and reflection for SDL_GPU (docs/notes/renderer.md, "HLSL and SDL_shadercross"), run by
CMakeLists.txt at build time. It reads each program's compiled stages (shadercross's output in --compiled:
<program>_<stage>.<format>) and their SPIR-V's reflection (spirv-cross --reflect: <program>_<stage>.json), checks
the binding rules and writes two files into --out:

- gpu_shaders.h: the uniform blocks' and storage buffers' structs, each program's slots and counts, and the program
  table's types;
- gpu_shaders.cpp: the program table with every stage's code.

usage: python tools/gen_gpu_shaders.py --sources DIR --compiled DIR --out DIR --formats spirv,msl[,dxil] PROGRAM...
"""
import argparse
import json
import os
import re
import sys

# SDL_GPU's register spaces: (texture and sampler set, uniform block set) per stage.
SETS = {'vertex': (0, 1), 'fragment': (2, 3)}
ENTRIES = {'vertex': 'vs_main', 'fragment': 'ps_main'}
# Block member types: (C++ scalar, components). A matrix is a Matrix4, the rest a scalar or an array of them.
MEMBERS = {'float': ('float', 1), 'vec2': ('float', 2), 'vec3': ('float', 3), 'vec4': ('float', 4),
           'int': ('int', 1), 'ivec2': ('int', 2), 'ivec3': ('int', 3), 'ivec4': ('int', 4),
           'uint': ('std::uint32_t', 1), 'uvec2': ('std::uint32_t', 2), 'uvec3': ('std::uint32_t', 3),
           'uvec4': ('std::uint32_t', 4), 'mat4': ('Matrix4', 16)}
# Vertex inputs and fragment outputs: SPIR-V type to (components, scalar).
SCALARS = {'float': 'Float', 'vec': 'Float', 'int': 'Int', 'ivec': 'Int', 'uint': 'Uint', 'uvec': 'Uint'}
TEXTURES = ('texture2D', 'utexture2D', 'itexture2D', 'texture2DMS', 'texture2DArray')
ALLOWED = {'entryPoints', 'types', 'inputs', 'outputs', 'ubos', 'ssbos', 'separate_images', 'separate_samplers'}

errors = []


def fail(message):
    errors.append(message)


def camel(name):
    return ''.join(part[:1].upper() + part[1:] for part in name.split('_') if part)


def io_type(t):
    m = re.fullmatch(r'(u|i)?vec([234])', t)
    if m:
        return int(m.group(2)), {'u': 'Uint', 'i': 'Int', None: 'Float'}[m.group(1)]
    if t in ('float', 'int', 'uint'):
        return 1, SCALARS[t]
    return None


def source_text(sources, name, seen):
    """The program's file with its includes in order (each once)."""
    if name in seen:
        return ''
    seen.add(name)
    text = open(os.path.join(sources, name), encoding='utf-8').read()
    out = []
    for line in text.splitlines():
        m = re.match(r'\s*#include\s+"([^"]+)"', line)
        out.append(source_text(sources, m.group(1), seen) if m else line)
    return '\n'.join(out)


def check_locations(program, text):
    """Every [[vk::location(n)]] field is TEXCOORDn, so DXIL's semantics and SPIR-V's locations agree."""
    for m in re.finditer(r'\[\[vk::location\((\d+)\)\]\][^;:]*?(\w+)\s*:\s*(\w+)', text):
        if m.group(3) != f'TEXCOORD{m.group(1)}':
            fail(f'{program}: {m.group(2)} has vk::location({m.group(1)}) but semantic {m.group(3)}')


def vertex_inputs(program, text):
    """The vertex stage's inputs by location: their HLSL field names."""
    m = re.search(r'\bvs_main\s*\(([^)]*)\)', text)
    if not m:
        fail(f'{program}: no vs_main')
        return {}
    params = [p.strip() for p in m.group(1).split(',') if 'SV_VertexID' not in p and p.strip()]
    if not params:   # only the vertex's index
        return {}
    struct = params[0].split()[0]
    s = re.search(r'\bstruct\s+' + struct + r'\s*\{(.*?)\};', text, re.S)
    if not s:
        fail(f'{program}: vs_main takes {struct}, not found')
        return {}
    names = {}
    for f in re.finditer(r'\[\[vk::location\((\d+)\)\]\]\s*(?:\w+\s+)*?\w+\s+(\w+)\s*:\s*TEXCOORD\d+', s.group(1)):
        names[int(f.group(1))] = f.group(2)
    return names


def block_struct(program, stage, j, ubo):
    """A block's members as HLSL packs a constant buffer (DXIL's layout; the SPIR-V's offsets must agree): a member
    follows the one before unless it would cross a 16-byte register, then it starts the next; a matrix and an array
    start a register, and an array's elements one each (so arrays are of 16-byte members only). Each member is
    (name, C++ type, extent, offset, end)."""
    name = ubo['name']
    if not name.startswith('type.ConstantBuffer.'):
        fail(f'{program} {stage}: block {name} is not a ConstantBuffer<T>')
        return None
    name = name[len('type.ConstantBuffer.'):]
    members, at = [], 0
    for m in j['types'][ubo['type']]['members']:
        if m['type'] not in MEMBERS:
            fail(f'{program}: {name}.{m["name"]} is {m["type"]}: members are float, int or uint (1 to 4 components) '
                 'or row_major float4x4')
            continue
        if m['type'] == 'mat4' and (m.get('row_major') or m.get('matrix_stride') != 16):
            fail(f'{program}: {name}.{m["name"]} must be row_major float4x4 (a register a Matrix4 row)')
        ctype, components = MEMBERS[m['type']]
        size = components * 4
        array = m.get('array', [])
        if len(array) > 1:
            fail(f'{program}: {name}.{m["name"]}: one array dimension at most')
        if array and size % 16:
            fail(f'{program}: {name}.{m["name"]}: an array of {m["type"]} (arrays are of 16-byte members)')
        if array and m.get('array_stride', size) != size:
            fail(f'{program}: {name}.{m["name"]}: array stride {m.get("array_stride")}, not {size}')
        count = array[0] if array else 1
        offset = at
        if array or ctype == 'Matrix4' or at // 16 != (at + size - 1) // 16:
            offset = (at + 15) // 16 * 16
        if m['offset'] != offset:
            fail(f'{program}: {name}.{m["name"]} at {m["offset"]}, HLSL\'s packing puts it at {offset}')
        extent = '' if ctype == 'Matrix4' else (f'[{count}]' if array else '') + (f'[{components}]' if components > 1
                                                                                  else '')
        at = offset + size * count
        members.append((m['name'], ctype, extent, offset, at))
    if ubo['block_size'] != at:
        fail(f'{program}: {name} is {ubo["block_size"]} bytes, its members {at}')
    return name, tuple(members), at


def storage_struct(program, stage, j, ssbo):
    """A read-only StructuredBuffer<T>'s element T as block_struct takes a block, its members 16 bytes each (float4,
    int4, uint4, arrays of them, row_major float4x4): DXIL packs a structured buffer's members tightly, SPIR-V by
    std430 and the MSL as the SPIR-V, which agree only then."""
    name = j['types'][ssbo['type']]['name']   # the variable's name is ssbo['name']
    if not name.startswith('type.StructuredBuffer.'):
        fail(f'{program} {stage}: storage buffer {name} is not a StructuredBuffer<T>')
        return None
    outer = j['types'][ssbo['type']]['members']
    if len(outer) != 1 or outer[0].get('array') != [0]:
        fail(f'{program} {stage}: storage buffer {name} is not one runtime array')
        return None
    element = {'name': 'type.ConstantBuffer.' + name[len('type.StructuredBuffer.'):], 'type': outer[0]['type']}
    for m in j['types'][element['type']]['members']:
        if m['type'] not in ('vec4', 'ivec4', 'uvec4', 'mat4'):
            fail(f'{program}: {name}.{m["name"]} is {m["type"]}: a storage buffer\'s members are 16 bytes each')
    size = outer[0].get('array_stride', 0)
    b = block_struct(program, stage, j, dict(element, block_size=size))
    if b and size % 16:
        fail(f'{program}: {name}\'s elements are {size} bytes, not a multiple of 16')
    return b


def stage_info(program, stage, j):
    texture_set, block_set = SETS[stage]
    for key in j:
        if key not in ALLOWED:
            fail(f'{program} {stage}: {key} (only uniform blocks, textures and samplers)')
    if [e['name'] for e in j.get('entryPoints', [])] != [ENTRIES[stage]]:
        fail(f'{program} {stage}: entry points {j.get("entryPoints")}')
    blocks, images, samplers, storage = {}, {}, {}, {}
    for u in j.get('ubos', []):
        if u['set'] != block_set:
            fail(f'{program} {stage}: block {u["name"]} in space{u["set"]}, not space{block_set}')
        b = block_struct(program, stage, j, u)
        if b:
            blocks[u['binding']] = b
    for i in j.get('separate_images', []):
        if i['set'] != texture_set:
            fail(f'{program} {stage}: texture {i["name"]} in space{i["set"]}, not space{texture_set}')
        if i['type'] not in TEXTURES:
            fail(f'{program} {stage}: texture {i["name"]} is {i["type"]}')
        images[i['binding']] = i['name']
    for s in j.get('separate_samplers', []):
        if s['set'] != texture_set:
            fail(f'{program} {stage}: sampler {s["name"]} in space{s["set"]}, not space{texture_set}')
        samplers[s['binding']] = s['name']
    # A storage buffer after the stage's textures, at the SPIR-V binding SDL_GPU gives its slot.
    textures = len(j.get('separate_images', []))
    for slot, u in enumerate(sorted(j.get('ssbos', []), key=lambda u: u['binding'])):
        if u['set'] != texture_set or u['binding'] != textures + slot:
            fail(f'{program} {stage}: storage buffer {u["name"]} at space{u["set"]} binding {u["binding"]}, not '
                 f'space{texture_set} binding {textures + slot} (after the textures)')
        b = storage_struct(program, stage, j, u)
        if b:
            storage[slot] = b
    for what, slots in (('block', blocks), ('texture', images)):
        if sorted(slots) != list(range(len(slots))):
            fail(f'{program} {stage}: {what} slots {sorted(slots)}: they run from 0 without gaps')
    if len(blocks) > 4 or len(images) > 16:
        fail(f'{program} {stage}: {len(blocks)} blocks, {len(images)} textures (SDL_GPU: at most 4 and 16)')
    for slot, name in samplers.items():
        if slot not in images:
            fail(f'{program} {stage}: sampler {name} at slot {slot} has no texture there')
    if stage == 'vertex' and (images or samplers):
        fail(f'{program}: the vertex stage takes no textures (none of the renderer\'s does)')
    inputs = []
    for i in j.get('inputs', []) if stage == 'vertex' else []:
        t = io_type(i['type'])
        if not t:
            fail(f'{program}: input {i["name"]} is {i["type"]}')
            continue
        inputs.append((i['location'], t[0], t[1]))
    outputs = []
    for o in j.get('outputs', []) if stage == 'fragment' else []:
        t = io_type(o['type'])
        if not t:
            fail(f'{program}: output {o["name"]} is {o["type"]}')
            continue
        outputs.append((o['location'], t[0], t[1]))
    return {'blocks': blocks, 'images': images, 'storage': storage, 'inputs': sorted(inputs),
            'outputs': sorted(outputs)}


def msl_storage_slots(text, j):
    """The MSL with each storage buffer moved from its SPIR-V binding to SDL_GPU's Metal slot, after the uniform
    blocks (spirv-cross's --msl-decoration-binding gives it the binding, which SDL_GPU's Vulkan wants after the
    textures)."""
    blocks = len(j.get('ubos', []))
    for slot, u in enumerate(sorted(j.get('ssbos', []), key=lambda u: u['binding'])):
        name = j['types'][u['type']]['name'].replace('.', '_')
        text = re.sub(r'(\b' + name + r'&? \w+ \[\[buffer\()\d+(\)\]\])',
                      lambda m: f'{m.group(1)}{blocks + slot}{m.group(2)}', text)
    return text


def check_msl(program, stage, text, j):
    """Each texture, sampler and block of the MSL entry point at its SPIR-V binding (SDL_GPU's Metal slots), no two
    of a kind at one index. The MSL leaves out what the stage never reads, so only what it has is checked."""
    m = re.search(r'^(?:vertex|fragment) \S+ ' + ENTRIES[stage] + r'\((.*)\)\s*$', text, re.M)
    if not m:
        fail(f'{program} {stage}: no {ENTRIES[stage]} in the MSL')
        return
    want = {'texture': {i['name']: i['binding'] for i in j.get('separate_images', [])},
            'sampler': {s['name']: s['binding'] for s in j.get('separate_samplers', [])},
            'buffer': {u['name'].replace('.', '_'): u['binding'] for u in j.get('ubos', [])}}
    for slot, u in enumerate(sorted(j.get('ssbos', []), key=lambda u: u['binding'])):
        want['buffer'][j['types'][u['type']]['name'].replace('.', '_')] = len(j.get('ubos', [])) + slot
    seen = {kind: {} for kind in want}
    for param in re.finditer(r'([\w<>]+)&? (\w+) \[\[(texture|sampler|buffer)\((\d+)\)\]\]', m.group(1)):
        kind, index = param.group(3), int(param.group(4))
        name = param.group(1) if kind == 'buffer' else param.group(2)   # a block by its type's name
        if want[kind].get(name) != index:
            fail(f'{program} {stage}: MSL {kind} {name} at {index}, its SPIR-V binding {want[kind].get(name)}')
        if index in seen[kind]:
            fail(f'{program} {stage}: MSL {kind}s {seen[kind][index]} and {name} both at {index}')
        seen[kind][index] = name


def array(name, data, terminate):
    data = data + (b'\0' if terminate else b'')
    lines = [','.join(str(b) for b in data[i:i + 28]) + ',' for i in range(0, len(data), 28)]
    return f'const unsigned char {name}[] = {{\n' + '\n'.join(lines) + '\n};\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sources', required=True)
    ap.add_argument('--compiled', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--formats', required=True)
    ap.add_argument('programs', nargs='+')
    a = ap.parse_args()
    formats = a.formats.split(',')
    programs = sorted(a.programs)

    structs = {}   # name: (members, size, first program)
    info = {}
    msl = {}       # (program, stage): the MSL with its storage buffers' slots (msl_storage_slots)
    for p in programs:
        text = source_text(a.sources, p + '.hlsl', set())
        check_locations(p, text)
        names = vertex_inputs(p, text)
        info[p] = {}
        for stage in SETS:
            with open(os.path.join(a.compiled, f'{p}_{stage}.json'), encoding='utf-8') as f:
                s = stage_info(p, stage, json.load(f))
            info[p][stage] = s
            if 'msl' in formats:
                with open(os.path.join(a.compiled, f'{p}_{stage}.msl'), encoding='utf-8') as f:
                    with open(os.path.join(a.compiled, f'{p}_{stage}.json'), encoding='utf-8') as g:
                        reflection = json.load(g)
                        msl[(p, stage)] = msl_storage_slots(f.read(), reflection)
                        check_msl(p, stage, msl[(p, stage)], reflection)
            for name, members, size in list(s['blocks'].values()) + list(s['storage'].values()):
                if name in structs and structs[name][:2] != (members, size):
                    fail(f'{p}: block {name} differs from {structs[name][2]}\'s')
                structs.setdefault(name, (members, size, p))
        locations = [i[0] for i in info[p]['vertex']['inputs']]
        if sorted(names) != locations:
            fail(f'{p}: vs_main\'s fields at {sorted(names)}, the SPIR-V\'s inputs at {locations}')
        info[p]['names'] = names
    if errors:
        for e in errors:
            print(f'gen_gpu_shaders: {e}', file=sys.stderr)
        return 1

    h = ['// The HLSL programs (src/render/shaders/*.hlsl) as SDL_GPU takes them, written by tools/gen_gpu_shaders.py',
         '// from shadercross\'s output and spirv-cross\'s reflection (CMakeLists.txt); not committed.',
         '#pragma once', '#include <cstddef>', '#include <cstdint>', '', '#include "lib/vecmath.h"', '',
         'namespace gpu_shaders {', '',
         '// A stage\'s code in one format; empty where this system\'s build makes none (DXIL off Windows). MSL is text',
         '// and has a 0 after its `size` bytes.',
         'struct Code {', '    const unsigned char* bytes;', '    std::size_t size;', '};',
         'enum class Scalar { Float, Int, Uint };',
         '// What SDL_CreateGPUShader takes besides the code: the entry point, the texture-sampler pairs and the uniform',
         '// blocks.',
         'struct Stage {', '    Code spirv, dxil, msl;', '    const char* entry;', '    int samplers;',
         '    int uniformBlocks;', '    int storageBuffers;', '};',
         '// A vertex input (its location) or a colour output (its target) and its type in the shader.',
         'struct Value {', '    int location;', '    int components;', '    Scalar scalar;', '};',
         'struct Program {', '    const char* name;', '    Stage vertex, fragment;', '    const Value* inputs;',
         '    int inputCount;', '    const Value* outputs;', '    int outputCount;', '};', '',
         f'inline constexpr int kProgramCount = {len(programs)};',
         'extern const Program kPrograms[kProgramCount];', '',
         '// The uniform blocks and the storage buffers\' elements as the C++ fills them, laid out as HLSL packs a block',
         '// (a float3 a float[3], a Matrix4 a row_major float4x4): `paddingN` fills the rest of a register where the',
         '// next member starts a new one, and the last; each is pushed or written whole (sizeof).']

    def padding(at, to, index):
        words = (to - at) // 4
        return f'    std::uint32_t padding{index}' + (f'[{words}]' if words > 1 else '') + ';'

    for name, (members, size, _) in sorted(structs.items()):
        h.append(f'struct {name} {{')
        pads, at = 0, 0
        for member, ctype, extent, offset, member_end in members:
            if offset != at:
                h.append(padding(at, offset, pads))
                pads += 1
            h.append(f'    {ctype} {member}{extent};')
            at = member_end
        end = (size + 15) // 16 * 16
        if size != end:
            h.append(padding(size, end, pads))
        h += ['};', f'static_assert(sizeof({name}) == {end});']
        h += [f'static_assert(offsetof({name}, {member}) == {offset});' for member, _, _, offset, _ in members]
    h += ['', '// Each program\'s index in kPrograms and its slots: a block\'s (SDL_PushGPU{Vertex,Fragment}UniformData), a',
          '// texture\'s with its sampler (SDL_BindGPU{Vertex,Fragment}Samplers), a storage buffer\'s',
          '// (SDL_BindGPU{Vertex,Fragment}StorageBuffers; one both stages read at the same slot is named once) and a vertex',
          '// input\'s location.']
    for index, p in enumerate(programs):
        h.append(f'namespace {p} {{')
        lines = [('kIndex', index, None)]
        for stage in SETS:
            s = info[p][stage]
            lines += [(f'kBlock{b[0]}', slot, stage) for slot, b in sorted(s['blocks'].items())]
            lines += [(f'kSampler{camel(n)}', slot, stage) for slot, n in sorted(s['images'].items())]
            lines += [(f'kStorage{b[0]}', slot, stage) for slot, b in sorted(s['storage'].items())]
        lines += [(f'kAttr{camel(n)}', loc, None) for loc, n in sorted(info[p]['names'].items())]
        seen = {}   # name: (value, its line in h)
        for name, value, stage in lines:
            if name in seen and name.startswith('kStorage') and seen[name][0] == value:
                h[seen[name][1]] = f'inline constexpr int {name} = {value};   // vertex and fragment'
                continue
            if name in seen:
                print(f'gen_gpu_shaders: {p}: two slots named {name}', file=sys.stderr)
                return 1
            seen[name] = (value, len(h))
            h.append(f'inline constexpr int {name} = {value};' + (f'   // {stage}' if stage else ''))
        h.append(f'}}  // namespace {p}')
    h += ['', '}  // namespace gpu_shaders', '']

    c = ['// The HLSL programs\' code (gpu_shaders.h), written by tools/gen_gpu_shaders.py; not committed.',
         '#include "gpu_shaders.h"', '', 'namespace gpu_shaders {', 'namespace {', '']
    table = []
    for p in programs:
        stages = []
        for stage in SETS:
            s = info[p][stage]
            codes = []
            for fmt in ('spirv', 'dxil', 'msl'):
                if fmt not in formats:
                    codes.append('{nullptr, 0}')
                    continue
                if fmt == 'msl':
                    data = msl[(p, stage)].encode('utf-8')
                else:
                    with open(os.path.join(a.compiled, f'{p}_{stage}.{fmt}'), 'rb') as f:
                        data = f.read()
                var = f'k{camel(p)}{camel(stage)}{camel(fmt)}'
                c.append(array(var, data, fmt == 'msl'))
                codes.append(f'{{{var}, {len(data)}}}')
            stages.append(f'{{{codes[0]}, {codes[1]}, {codes[2]}, "{ENTRIES[stage]}", {len(s["images"])}, '
                          f'{len(s["blocks"])}, {len(s["storage"])}}}')
        refs = []
        for what, values in (('Inputs', info[p]['vertex']['inputs']), ('Outputs', info[p]['fragment']['outputs'])):
            if values:
                var = f'k{camel(p)}{what}'
                c.append(f'const Value {var}[] = {{' +
                         ', '.join(f'{{{l}, {n}, Scalar::{t}}}' for l, n, t in values) + '};')
                refs.append(f'{var}, {len(values)}')
            else:
                refs.append('nullptr, 0')
        table.append(f'    {{"{p}", {stages[0]},\n     {stages[1]},\n     {refs[0]}, {refs[1]}}},')
    c += ['', '}  // namespace', '', 'const Program kPrograms[kProgramCount] = {'] + table + ['};', '',
          '}  // namespace gpu_shaders', '']

    os.makedirs(a.out, exist_ok=True)
    for name, lines in (('gpu_shaders.h', h), ('gpu_shaders.cpp', c)):
        path = os.path.join(a.out, name)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines))
    return 0


if __name__ == '__main__':
    sys.exit(main())
