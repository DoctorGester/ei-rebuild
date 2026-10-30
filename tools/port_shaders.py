#!/usr/bin/env python3
"""Package existing HLSL as sokol shader descriptors.

Metal/GL convert through Vulkan GLSL and sokol-shdc; Windows wraps compiled fxc headers.
HLSL remains authoritative until the shaders are authored directly for sokol-shdc.
"""
import argparse
from pathlib import Path
import re
import subprocess


def run(args):
    result = subprocess.run([str(arg) for arg in args], text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(" ".join(str(arg) for arg in args) + "\n" + result.stdout + result.stderr)
    return result.stdout


def stage_source(args, source, entry, stage, output, defines, fragment_base=1):
    spv = output.with_suffix("." + stage + ".spv")
    run([args.glslang, "-D", "-V", "-S", stage, "-e", entry, "--auto-map-bindings", "--auto-map-locations",
         "-I" + str(source.parent), *["-D" + d for d in defines], "-o", spv, source])
    text = run([args.spirv_cross, spv, "--vulkan-semantics"])
    text = re.sub(r"^#version[^\n]*\n", "", text, flags=re.M)
    text = re.sub(r"set = \d+, ", "", text)
    text = re.sub(r"(std140\) uniform )(\w+)", r"\1ei_" + stage + r"_\2", text)
    if stage == "frag":
        # Fragment-only uniforms use slot 0; shaders with vertex uniforms start fragment slots at 1.
        text = re.sub(r"layout\(binding = (\d+), std140\)",
                      lambda m: "layout(binding = " + str(int(m[1]) + fragment_base) + ", std140)", text)
    # Match vertex outputs to fragment inputs by their locations, rather than HLSL's generated variable names.
    direction = "out" if stage == "vert" else "in"
    for location, name in re.findall(r"layout\(location = (\d+)\) (?:flat )?" + direction + r" \w+ (\w+);", text):
        text = re.sub(r"\b" + re.escape(name) + r"\b", "ei_varying_" + location, text)
    # HLSL Load has no sampler. GL's combined sampler API still needs one, as do sokol's bindings.
    textures = re.findall(r"layout\(binding = (\d+)\) uniform (\w*texture\w+) (\w+);", text)
    samplers = re.findall(r"layout\(binding = (\d+)\) uniform sampler (\w+);", text)
    dummy = samplers[0][1] if samplers else "ei_load_sampler"
    used_dummy = False
    for binding, kind, name in textures:
        sampler_type = kind.replace("texture", "sampler")
        pattern = r"texelFetch\(" + re.escape(name) + r","
        if re.search(pattern, text):
            text = re.sub(pattern, "texelFetch(" + sampler_type + "(" + name + ", " + dummy + "),", text)
            used_dummy = True
    if used_dummy and not samplers:
        text = "layout(binding = 0) uniform sampler ei_load_sampler;\n" + text
    # shdc flattens GL uniform blocks to vec4 arrays. A dynamic component of a dynamic vector cannot be
    # represented by that flattening; the point-shadow slots are exact small integers, so select by dot product.
    text = re.sub(r"(\w+\.point_slots)\[([^\]]+)\]\[([^\]]+)\]",
                  r"dot(\1[\2], vec4(equal(ivec4(0, 1, 2, 3), ivec4(\3))))", text)
    # The clouds' packed uint words keep their bits in signed ivec4 uniform storage (shdc's portable subset).
    unsigned = []
    def signed_block(match):
        body, instance = match[2], match[4]
        for width, member, array in re.findall(r"uvec([234]) (\w+)(\[\d+\])?;", body):
            unsigned.append((instance, member, width, bool(array)))
        body = re.sub(r"\buvec([234])\b", r"ivec\1", body)
        return match[1] + body + match[3]
    text = re.sub(r"(layout\([^\n]*std140\) uniform \w+\s*\{)(.*?)(\}\s*(\w+);)",
                  signed_block, text, flags=re.S)
    for instance, member, width, array in unsigned:
        pattern = r"\b" + re.escape(instance + "." + member) + (r"\[[^\]]+\]" if array else r"\b")
        text = re.sub(pattern, lambda m: "uvec" + width + "(" + m[0] + ")", text)
    # Packed ramp component selected without a dynamic uniform-vector subscript.
    ramp = r"uvec4\((\w+\.ramp\[[^\]]+\])\)\[([^\]]+)\]"
    if re.search(ramp, text):
        text = re.sub(ramp, r"ei_uint_component(uvec4(\1), uint(\2))", text)
        text = ("uint ei_uint_component(uvec4 v, uint k) { return v.x * uint(k == 0u) + "
                "v.y * uint(k == 1u) + v.z * uint(k == 2u) + v.w * uint(k == 3u); }\n" + text)
    # A single int4 array preserves the CPU bytes and fits sokol's 16-member GL reflection limit even for
    # mixed float/integer blocks. Reinterpret float words in the shader, rather than numerically converting them.
    members = []
    def packed_block(match):
        body, instance = match[2], match[4]
        offset = 0
        if re.sub(r"(?:i?vec4) \w+(?:\[\d+\])?;|\s+", "", body):
            raise RuntimeError("Unsupported shader uniform layout: " + body)
        for kind, member, count in re.findall(r"(i?vec4) (\w+)(?:\[(\d+)\])?;", body):
            members.append((instance, member, kind, offset, bool(count)))
            offset += int(count) if count else 1
        if not offset:
            raise RuntimeError("Unsupported shader uniform layout: " + body)
        return match[1] + "\n    ivec4 ei_words[" + str(offset) + "];\n" + match[3]
    text = re.sub(r"(layout\([^\n]*std140\) uniform \w+\s*\{)(.*?)(\}\s*(\w+);)",
                  packed_block, text, flags=re.S)
    for instance, member, kind, offset, array in members:
        pattern = r"\b" + re.escape(instance + "." + member) + (r"\[([^\]]+)\]" if array else r"\b")
        def access(match):
            index = str(offset) + (" + int(" + match[1] + ")" if array else "")
            value = instance + ".ei_words[" + index + "]"
            return "intBitsToFloat(" + value + ")" if kind == "vec4" else value
        text = re.sub(pattern, access, text)
    return text


def wrap_dxbc(headers, output, programs):
    """Expose the existing fxc arrays through the same descriptor interface as sokol-shdc."""
    pairs = []
    for name, vs, ve, fs, fe, defines in programs:
        vertex = vs if vs.endswith("_vs") else vs + "_vs"
        if vs == "ui_canvas":
            vertex = "ui_glyph_vs" if ve == "vs_glyph" else "ui_rect_vs"
        fragment = name + "_ps"
        pairs.append((name, vertex, fragment))
    arrays = sorted({array for name, vertex, fragment in pairs for array in (vertex, fragment)})
    for array in arrays:
        if not (headers / (array + ".h")).is_file():
            raise RuntimeError("Missing fxc output: " + str(headers / (array + ".h")))
    text = '#pragma once\n#include "sokol_gfx.h"\nnamespace dxbc_generated {\nusing BYTE = unsigned char;\n'
    text += "".join('#include "shaders/' + array + '.h"\n' for array in arrays) + "}\n"
    for name, vertex, fragment in pairs:
        text += ("namespace " + name + "_generated {\ninline const sg_shader_desc* " + name +
                 "_shader_desc(sg_backend) {\n    static const sg_shader_desc desc = [] {\n"
                 "        sg_shader_desc result{};\n        result.vertex_func.bytecode = SG_RANGE(dxbc_generated::" +
                 vertex + ");\n        result.fragment_func.bytecode = SG_RANGE(dxbc_generated::" + fragment +
                 ");\n        return result;\n    }();\n    return &desc;\n}\n}\n")
    output.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, required=True)
    compiler = parser.add_mutually_exclusive_group(required=True)
    compiler.add_argument("--shdc")
    compiler.add_argument("--dxbc-headers", type=Path, help="Wrap already compiled Windows fxc headers")
    parser.add_argument("--glslang", default="glslangValidator")
    parser.add_argument("--spirv-cross", default="spirv-cross")
    parser.add_argument("--bytecode", action="store_true")
    args = parser.parse_args()
    source = Path(__file__).resolve().parent.parent / "src/render/shaders"
    args.out_dir.mkdir(parents=True, exist_ok=True)
    programs = []
    for name in ["blit", "solid", "mesh", "terrain", "present", "shadow_caster", "shadow_terrain",
                 "into_multisampled", "perf_overlay", "texture_compose"]:
        programs.append((name, name, "vs_main", name, "ps_main", []))
    for samples in [2, 4, 8]:
        programs.append(("depth_from_multisampled" + str(samples), "depth_from_multisampled", "vs_main",
                         "depth_from_multisampled", "ps_main", ["SAMPLES=" + str(samples)]))
    programs += [("texture_clouds_frame", "texture_clouds", "vs_main", "texture_clouds", "ps_frame", []),
                 ("texture_clouds", "texture_clouds", "vs_main", "texture_clouds", "ps_clouds", []),
                 ("shadow_reduce", "shadow_range_vs", "vs_main", "shadow_reduce", "ps_main", []),
                 ("shadow_dilate", "shadow_range_vs", "vs_main", "shadow_dilate", "ps_main", [])]
    for entry in ["solid", "glyph", "compose", "grey", "copy", "rgb565", "blit"]:
        programs.append(("ui_" + entry, "ui_canvas", "vs_glyph" if entry == "glyph" else "vs_rect",
                         "ui_canvas", "ps_" + entry, []))
    if args.dxbc_headers:
        wrap_dxbc(args.dxbc_headers, args.out_dir / "all_shaders.h", programs)
        return
    includes = []
    for name, vs, ve, fs, fe, defines in programs:
        output = args.out_dir / name
        vertex = stage_source(args, source / (vs + ".hlsl"), ve, "vert", output, defines)
        fragment = stage_source(args, source / (fs + ".hlsl"), fe, "frag", output, defines,
                                fragment_base=1 if "std140) uniform" in vertex else 0)
        glsl = output.with_suffix(".glsl")
        glsl.write_text("@vs vs\n@glsl_options fixup_clipspace\n" + vertex + "\n@end\n@fs fs\n" + fragment +
                        "\n@end\n@program " + name + " vs fs\n")
        run([args.shdc, "-i", glsl, "-o", output.with_suffix(".h"), "-l", "metal_macos:glsl430",
             *(["-b"] if args.bytecode else []), "--no-log-cmdline"])
        macros = re.findall(r"^#define (\w+)", output.with_suffix(".h").read_text(), flags=re.M)
        includes.append('namespace ' + name + '_generated {\n#include "' + name + '.h"\n}\n' +
                        "".join("#undef " + macro + "\n" for macro in macros))
        print("Compiled " + name, flush=True)
    (args.out_dir / "all_shaders.h").write_text(
        '#pragma once\n#include "sokol_gfx.h"\n#include <stdint.h>\n#include <stdbool.h>\n#include <string.h>\n' +
        "".join(includes))


if __name__ == "__main__":
    main()
