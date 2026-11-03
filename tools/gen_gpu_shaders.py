"""The HLSL programs' code and reflection for SDL_GPU (docs/notes/renderer.md, "HLSL and SDL_shadercross"), run by
CMakeLists.txt at build time. It reads each program's compiled stages (shadercross's output in --compiled:
<program>_<stage>.<format>) and their SPIR-V's reflection (spirv-cross --reflect: <program>_<stage>.json), checks
the binding rules and writes two files into --out:

- gpu_shaders.h: the uniform blocks' structs, each program's slots and counts, and the program table's types;
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
# Block member types: (C++ type, its extent, bytes).
MEMBERS = {'vec4': ('float', '[4]', 16), 'ivec4': ('int', '[4]', 16), 'uvec4': ('std::uint32_t', '[4]', 16),
           'mat4': ('Matrix4', '', 64)}
# Vertex inputs and fragment outputs: SPIR-V type to (components, scalar).
SCALARS = {'float': 'Float', 'vec': 'Float', 'int': 'Int', 'ivec': 'Int', 'uint': 'Uint', 'uvec': 'Uint'}
TEXTURES = ('texture2D', 'utexture2D', 'itexture2D', 'texture2DMS')
ALLOWED = {'entryPoints', 'types', 'inputs', 'outputs', 'ubos', 'separate_images', 'separate_samplers'}

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
    params = m.group(1).strip()
    if not params or 'SV_VertexID' in params:
        return {}
    struct = params.split()[0]
    s = re.search(r'\bstruct\s+' + struct + r'\s*\{(.*?)\};', text, re.S)
    if not s:
        fail(f'{program}: vs_main takes {struct}, not found')
        return {}
    names = {}
    for f in re.finditer(r'\[\[vk::location\((\d+)\)\]\]\s*(?:\w+\s+)*?\w+\s+(\w+)\s*:\s*TEXCOORD\d+', s.group(1)):
        names[int(f.group(1))] = f.group(2)
    return names


def block_struct(program, stage, j, ubo):
    name = ubo['name']
    if not name.startswith('type.ConstantBuffer.'):
        fail(f'{program} {stage}: block {name} is not a ConstantBuffer<T>')
        return None
    name = name[len('type.ConstantBuffer.'):]
    members, at = [], 0
    for m in j['types'][ubo['type']]['members']:
        if m['type'] not in MEMBERS:
            fail(f'{program}: {name}.{m["name"]} is {m["type"]}: members are float4, int4, uint4 or row_major float4x4')
            continue
        if m['type'] == 'mat4' and (m.get('row_major') or m.get('matrix_stride') != 16):
            fail(f'{program}: {name}.{m["name"]} must be row_major float4x4 (a register a Matrix4 row)')
        ctype, extent, size = MEMBERS[m['type']]
        count = 1
        array = m.get('array', [])
        if len(array) > 1:
            fail(f'{program}: {name}.{m["name"]}: one array dimension at most')
        if array:
            count = array[0]
            if m.get('array_stride', size) != size:
                fail(f'{program}: {name}.{m["name"]}: array stride {m.get("array_stride")}, not {size}')
        if m['offset'] != at:
            fail(f'{program}: {name}.{m["name"]} at {m["offset"]}, not {at} (no padding: 16-byte members)')
        members.append((m['name'], ctype, (f'[{count}]' if array else '') + extent, m['type'], count if array else 0))
        at += size * count
    if ubo['block_size'] != at:
        fail(f'{program}: {name} is {ubo["block_size"]} bytes, its members {at}')
    return name, tuple(members), at


def stage_info(program, stage, j):
    texture_set, block_set = SETS[stage]
    for key in j:
        if key not in ALLOWED:
            fail(f'{program} {stage}: {key} (only uniform blocks, textures and samplers)')
    if [e['name'] for e in j.get('entryPoints', [])] != [ENTRIES[stage]]:
        fail(f'{program} {stage}: entry points {j.get("entryPoints")}')
    blocks, images, samplers = {}, {}, {}
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
    return {'blocks': blocks, 'images': images, 'inputs': sorted(inputs), 'outputs': sorted(outputs)}


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
                        check_msl(p, stage, f.read(), json.load(g))
            for name, members, size in s['blocks'].values():
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
         '    int uniformBlocks;', '};',
         '// A vertex input (its location) or a colour output (its target) and its type in the shader.',
         'struct Value {', '    int location;', '    int components;', '    Scalar scalar;', '};',
         'struct Program {', '    const char* name;', '    Stage vertex, fragment;', '    const Value* inputs;',
         '    int inputCount;', '    const Value* outputs;', '    int outputCount;', '};', '',
         f'inline constexpr int kProgramCount = {len(programs)};',
         'extern const Program kPrograms[kProgramCount];', '',
         '// The uniform blocks as the C++ fills them: 16-byte members (float4, int4) and Matrix4s, without padding.']
    for name, (members, size, _) in sorted(structs.items()):
        h.append(f'struct {name} {{')
        for member, ctype, extent, _, _ in members:
            h.append(f'    {ctype} {member}{extent};')
        h += ['};', f'static_assert(sizeof({name}) == {size});']
    h += ['', '// Each program\'s index in kPrograms and its slots: a block\'s (SDL_PushGPU{Vertex,Fragment}UniformData), a',
          '// texture\'s with its sampler (SDL_BindGPU{Vertex,Fragment}Samplers) and a vertex input\'s location.']
    for index, p in enumerate(programs):
        h.append(f'namespace {p} {{')
        lines = [('kIndex', index, None)]
        for stage in SETS:
            s = info[p][stage]
            lines += [(f'kBlock{b[0]}', slot, stage) for slot, b in sorted(s['blocks'].items())]
            lines += [(f'kSampler{camel(n)}', slot, stage) for slot, n in sorted(s['images'].items())]
        lines += [(f'kAttr{camel(n)}', loc, None) for loc, n in sorted(info[p]['names'].items())]
        seen = set()
        for name, value, stage in lines:
            if name in seen:
                print(f'gen_gpu_shaders: {p}: two slots named {name}', file=sys.stderr)
                return 1
            seen.add(name)
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
                with open(os.path.join(a.compiled, f'{p}_{stage}.{fmt}'), 'rb') as f:
                    data = f.read()
                var = f'k{camel(p)}{camel(stage)}{camel(fmt)}'
                c.append(array(var, data, fmt == 'msl'))
                codes.append(f'{{{var}, {len(data)}}}')
            stages.append(f'{{{codes[0]}, {codes[1]}, {codes[2]}, "{ENTRIES[stage]}", {len(s["images"])}, '
                          f'{len(s["blocks"])}}}')
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
