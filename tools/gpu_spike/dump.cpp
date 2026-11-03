// The SDL_GPU spike's frame dump (dump.h): written and read record by record.
#include "dump.h"

#include <cstring>

namespace gpu_spike {

bool WriteFrame(const char* path, const Frame& frame) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    Writer w(f);
    w.Raw(kMagic, sizeof(kMagic));
    w.Value(Sizes{});
    for (const Shader& s : frame.shaders) {
        w.Value(Record::Shader);
        w.Value(s.id);
        w.Text(s.label);
    }
    for (const Image& i : frame.images) {
        w.Value(Record::Image);
        w.Value(i.id);
        w.Value(i.desc);
        w.Text(i.label);
        w.Value(static_cast<std::uint32_t>(i.levels.size()));
        for (const auto& level : i.levels) w.Bytes(level);
    }
    for (const Buffer& b : frame.buffers) {
        w.Value(Record::Buffer);
        w.Value(b.id);
        w.Value(b.desc);
        w.Text(b.label);
        w.Bytes(b.bytes);
    }
    for (const Sampler& s : frame.samplers) {
        w.Value(Record::Sampler);
        w.Value(s.id);
        w.Value(s.desc);
    }
    for (const View& v : frame.views) {
        w.Value(Record::View);
        w.Value(v.id);
        w.Value(v.desc);
    }
    for (const Pipeline& p : frame.pipelines) {
        w.Value(Record::Pipeline);
        w.Value(p.id);
        w.Value(p.desc);
        w.Text(p.label);
    }
    for (const Command& c : frame.commands) {
        w.Value(c.kind);
        switch (c.kind) {
        case Record::BeginPass:
            w.Value(c.pass);
            w.Text(c.label);
            break;
        case Record::Viewport:
        case Record::Scissor:
            w.Raw(c.args, sizeof(int) * 4);
            w.Value(c.flag);
            break;
        case Record::ApplyPipeline: w.Value(c.id); break;
        case Record::ApplyBindings: w.Value(c.bindings); break;
        case Record::ApplyUniforms:
            w.Value(c.args[0]);
            w.Bytes(c.bytes);
            break;
        case Record::Draw: w.Raw(c.args, sizeof(c.args)); break;
        default: break;
        }
    }
    w.Value(Record::End);
    bool ok = w.ok;
    return std::fclose(f) == 0 && ok;
}

bool ReadFrame(const char* path, Frame* frame, std::string* error) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        *error = std::string("cannot open ") + path;
        return false;
    }
    Reader r(f);
    char magic[8];
    r.Raw(magic, sizeof(magic));
    const Sizes sizes = r.Value<Sizes>(), expected{};
    if (!r.ok || std::memcmp(magic, kMagic, sizeof(magic)) != 0 || std::memcmp(&sizes, &expected, sizeof(Sizes)) != 0) {
        std::fclose(f);
        *error = "not a frame dump of this sokol_gfx";
        return false;
    }
    for (;;) {
        Record kind = r.Value<Record>();
        if (!r.ok || kind == Record::End) break;
        switch (kind) {
        case Record::Shader: {
            Shader s;
            s.id = r.Value<std::uint32_t>();
            s.label = r.Text();
            frame->shaders.push_back(std::move(s));
            break;
        }
        case Record::Image: {
            Image i;
            i.id = r.Value<std::uint32_t>();
            i.desc = r.Value<sg_image_desc>();
            i.label = r.Text();
            std::uint32_t levels = r.Value<std::uint32_t>();
            for (std::uint32_t k = 0; k < levels && r.ok; ++k) i.levels.push_back(r.Bytes());
            frame->images.push_back(std::move(i));
            break;
        }
        case Record::Buffer: {
            Buffer b;
            b.id = r.Value<std::uint32_t>();
            b.desc = r.Value<sg_buffer_desc>();
            b.label = r.Text();
            b.bytes = r.Bytes();
            frame->buffers.push_back(std::move(b));
            break;
        }
        case Record::Sampler: {
            Sampler s;
            s.id = r.Value<std::uint32_t>();
            s.desc = r.Value<sg_sampler_desc>();
            frame->samplers.push_back(s);
            break;
        }
        case Record::View: {
            View v;
            v.id = r.Value<std::uint32_t>();
            v.desc = r.Value<sg_view_desc>();
            frame->views.push_back(v);
            break;
        }
        case Record::Pipeline: {
            Pipeline p;
            p.id = r.Value<std::uint32_t>();
            p.desc = r.Value<sg_pipeline_desc>();
            p.label = r.Text();
            frame->pipelines.push_back(std::move(p));
            break;
        }
        default: {
            Command c;
            c.kind = kind;
            switch (kind) {
            case Record::BeginPass:
                c.pass = r.Value<sg_pass>();
                c.label = r.Text();
                break;
            case Record::Viewport:
            case Record::Scissor:
                r.Raw(c.args, sizeof(int) * 4);
                c.flag = r.Value<bool>();
                break;
            case Record::ApplyPipeline: c.id = r.Value<std::uint32_t>(); break;
            case Record::ApplyBindings: c.bindings = r.Value<sg_bindings>(); break;
            case Record::ApplyUniforms:
                c.args[0] = r.Value<int>();
                c.bytes = r.Bytes();
                break;
            case Record::Draw: r.Raw(c.args, sizeof(c.args)); break;
            case Record::EndPass: break;
            default:
                r.ok = false;
                break;
            }
            frame->commands.push_back(std::move(c));
        }
        }
    }
    std::fclose(f);
    if (!r.ok) *error = "the dump is cut short or damaged";
    return r.ok;
}

} // namespace gpu_spike
