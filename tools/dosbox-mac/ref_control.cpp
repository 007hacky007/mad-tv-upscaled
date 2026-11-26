// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in, single-threaded reference controller. No host input or capture APIs.
#include "dosbox.h"
#include "pic.h"
#include "mem.h"
#include "paging.h"
#include "keyboard.h"
#include "mouse.h"
#include "vga.h"
#include "inout.h"
#include "regs.h"
#include "ref_control.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <png.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <vector>

extern bool noremark_save_state, use_save_file, reference_state_ok;
extern std::string savefilename;
extern void REF_MouseAbsolute(unsigned, unsigned);
namespace {
int listener = -1, peer = -1;
bool paused = true, stepping = false;
uint64_t frame_count = 0, tick_target = 0, frame_target = 0;
std::string pending, socket_path;
std::string iolog; // "ms port value" lines written by the guest to probe ports
bool io_registered = false;
std::vector<uint8_t> io_signature; // when set, log only writes whose far-call return address starts with these bytes
bool caller_matches() {
    if (io_signature.empty()) return true;
    const uint32_t sp = SegPhys(ss) + reg_sp;
    const uint16_t ip = mem_readw(sp), cs = mem_readw(sp + 2);
    const uint32_t ret = uint32_t(cs) * 16 + ip;
    for (size_t i = 0; i < io_signature.size(); ++i) if (mem_readb(ret + i) != io_signature[i]) return false;
    return true;
}
// AdLib register writes: "opllog on" puts a logger in front of the emulated OPL's own port handlers.
std::string opllog;
// The PIT ports 0x40 and 0x43 are logged too (the driver sets its tick rate there).
constexpr Bitu kLoggedPorts[] = {0x40, 0x43, 0x388, 0x389};
IO_WriteHandler* opl_prev[4] = {nullptr, nullptr, nullptr, nullptr};
void opl_port(Bitu port, Bitu val, Bitu len) {
    opllog += std::to_string(uint64_t(PIC_Ticks)) + " " + std::to_string(port) + " " + std::to_string(val) + "\n";
    for (size_t i = 0; i < 4; ++i) if (kLoggedPorts[i] == port) opl_prev[i](port, val, len);
}
void probe_port(Bitu port, Bitu val, Bitu) { if (!caller_matches()) return; iolog += std::to_string(uint64_t(PIC_Ticks)) + " " + std::to_string(port) + " " + std::to_string(val) + "\n"; }
std::vector<unsigned char> frame;
std::vector<unsigned char> raw_frame(640*480);
bool raw_valid=false;
unsigned raw_lines=0;
png_color raw_palette[256];
png_color palette[256];
unsigned frame_w=0, frame_h=0, frame_bpp=0;
unsigned cap_w=0, cap_h=0; bool cap_rgb=false; // dimensions/format of `frame`
void cleanup() { if (!socket_path.empty()) unlink(socket_path.c_str()); }
void disconnect() {
    if (peer >= 0) close(peer);
    peer = -1; pending.clear(); paused = true; stepping = false;
}
std::string status() {
    std::ostringstream s;
    s << "{\"ok\":true,\"protocol\":1,\"paused\":" << (paused?"true":"false")
      << ",\"ticks\":" << PIC_Ticks << ",\"frames\":" << frame_count
      << ",\"width\":" << frame_w << ",\"height\":" << frame_h << ",\"bpp\":" << frame_bpp
      << ",\"memory_bytes\":" << uint64_t(MEM_TotalPages())*4096 << "}\n";
    return s.str();
}
void reply(const std::string &s) {
    size_t pos = 0;
    while (peer >= 0 && pos < s.size()) {
        ssize_t n = send(peer, s.data()+pos, s.size()-pos, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { disconnect(); return; }
        pos += size_t(n);
    }
}
uint64_t number(const std::string &s, uint64_t max) {
    if (s.empty() || s[0] == '-' || s[0] == '+') throw std::runtime_error("number");
    size_t end = 0; auto n = std::stoull(s, &end, 0);
    if (end != s.size() || n > max) throw std::runtime_error("number");
    return n;
}
KBD_KEYS key(const std::string &s) {
#define K(n) if (s == #n) return KBD_##n
    K(esc); K(enter); K(space); K(tab); K(backspace);
    K(left); K(right); K(up); K(down); K(home); K(end); K(delete); K(period);
    K(leftshift); K(rightshift); K(leftctrl); K(leftalt);
    K(f1); K(f2); K(f3); K(f4); K(f5); K(f6); K(f7); K(f8); K(f9); K(f10); K(f11); K(f12);
    K(a); K(b); K(c); K(d); K(e); K(f); K(g); K(h); K(i); K(j); K(k); K(l); K(m);
    K(n); K(o); K(p); K(q); K(r); K(s); K(t); K(u); K(v); K(w); K(x); K(y); K(z);
    K(0); K(1); K(2); K(3); K(4); K(5); K(6); K(7); K(8); K(9);
#undef K
    throw std::runtime_error("key");
}
bool down(const std::string &s) {
    if (s != "down" && s != "up") throw std::runtime_error("direction");
    return s == "down";
}
void png(const std::string &path) {
    if (frame.empty() || !cap_w || !cap_h) throw std::runtime_error("no captured frame");
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("open PNG");
    png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = p ? png_create_info_struct(p) : nullptr;
    if (!p || !info) { if (p) png_destroy_write_struct(&p, nullptr); fclose(f); throw std::runtime_error("PNG allocation"); }
    if (setjmp(png_jmpbuf(p))) { png_destroy_write_struct(&p, &info); fclose(f); throw std::runtime_error("PNG write"); }
    png_init_io(p, f);
    png_set_IHDR(p, info, cap_w, cap_h, 8, cap_rgb ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_PALETTE,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    if (!cap_rgb) png_set_PLTE(p, info, palette, 256);
    png_write_info(p, info);
    const unsigned stride = cap_w * (cap_rgb ? 3 : 1);
    for (unsigned y=0; y<cap_h; ++y) png_write_row(p, frame.data()+y*stride);
    png_write_end(p, info); png_destroy_write_struct(&p, &info);
    if (fclose(f)) throw std::runtime_error("PNG close");
}
void command(const std::string &line) {
    try {
        std::istringstream in(line); std::vector<std::string> a; std::string word;
        if (line.find('\0')!=std::string::npos) throw std::runtime_error("syntax");
        while (true) {
            in >> std::ws;
            if (in.eof()) break;
            if (!(in >> std::quoted(word))) throw std::runtime_error("syntax");
            a.push_back(word);
        }
        if (a.empty()) throw std::runtime_error("syntax");
        const auto &op=a[0];
        if (op=="status" && a.size()==1) {}
        else if (op=="pause" && a.size()==1) { paused=true; stepping=false; }
        else if (op=="resume" && a.size()==1) { paused=false; stepping=false; }
        else if (op=="step" && a.size()==3 && !stepping) {
            uint64_t n=number(a[2], a[1]=="frames"?36000:600000);
            if (!n || (a[1]!="ms" && a[1]!="frames")) throw std::runtime_error("step");
            tick_target=a[1]=="ms"?uint64_t(PIC_Ticks)+n:0;
            frame_target=a[1]=="frames"?frame_count+n:0;
            paused=false; stepping=true; return; // reply only after emulated time elapsed
        }
        else if (op=="key" && a.size()==3) { auto k=key(a[1]); bool d=down(a[2]); KEYBOARD_AddKey(k,d); }
        else if (op=="mouse" && a.size()==3) {
            auto x=number(a[1],65535), y=number(a[2],65535);
            REF_MouseAbsolute(unsigned(x),unsigned(y));
        }
        else if (op=="button" && a.size()==3) {
            auto b=number(a[1],2); bool d=down(a[2]);
            if (d) Mouse_ButtonPressed(uint8_t(b)); else Mouse_ButtonReleased(uint8_t(b));
        }
        else if (op=="frame" && a.size()==2) png(a[1]);
        else if (op=="memory" && a.size()==4) {
            auto start=number(a[1],UINT32_MAX), n=number(a[2],uint64_t(MEM_TotalPages())*4096);
            if (!n || start+n>uint64_t(MEM_TotalPages())*4096) throw std::runtime_error("range");
            std::vector<uint8_t> bytes(n);
            for (uint64_t i=0;i<n;++i)
                if (mem_readb_checked(LinearPt(start+i), &bytes[i])) throw std::runtime_error("unmapped memory");
            std::ofstream f(a[3],std::ios::binary|std::ios::trunc);
            f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()); f.close();
            if (!f) throw std::runtime_error("memory file");
        }
        else if (op=="iosig" && a.size()==2) {
            if (a[1].size() % 2 || a[1].size() > 64) throw std::runtime_error("signature");
            io_signature.clear();
            for (size_t i = 0; i < a[1].size(); i += 2) io_signature.push_back(uint8_t(std::stoul(a[1].substr(i, 2), nullptr, 16)));
        }
        else if (op=="opllog" && a.size()==2 && a[1]=="on") {
            for (size_t i = 0; i < 4; ++i) {
                const Bitu p = kLoggedPorts[i];
                if (io_writehandlers[0][p] != opl_port) { opl_prev[i] = io_writehandlers[0][p]; io_writehandlers[0][p] = opl_port; }
            }
        }
        else if (op=="opllog" && a.size()==2) {
            std::ofstream f(a[1],std::ios::binary|std::ios::trunc);
            f << opllog; f.close();
            if (!f) throw std::runtime_error("opllog file");
            opllog.clear();
        }
        else if (op=="iolog" && a.size()==2) {
            std::ofstream f(a[1],std::ios::binary|std::ios::trunc);
            f << iolog; f.close();
            if (!f) throw std::runtime_error("iolog file");
            iolog.clear();
        }
        else if (op=="vram" && a.size()==2) {
            std::ofstream f(a[1],std::ios::binary|std::ios::trunc);
            f.write(reinterpret_cast<const char*>(vga.mem.linear),std::streamsize(vga.mem.memsize)); f.close();
            if (!f) throw std::runtime_error("vram file");
        }
        else if (op=="dac" && a.size()==2) {
            std::ofstream f(a[1],std::ios::binary|std::ios::trunc);
            for (unsigned i=0;i<256;++i) { char c[3]={char(vga.dac.rgb[i].red),char(vga.dac.rgb[i].green),char(vga.dac.rgb[i].blue)}; f.write(c,3); }
            for (unsigned i=0;i<16;++i) { char c=char(vga.attr.palette[i]); f.write(&c,1); }
            f.close();
            if (!f) throw std::runtime_error("dac file");
        }
        else if ((op=="save" || op=="load") && a.size()==2 && paused) {
            // The upstream API reports errors through a flag patched at its actual completion sites.
            bool old_use=use_save_file, old_remark=noremark_save_state;
            std::string old_name=savefilename;
            use_save_file=true; noremark_save_state=true; savefilename=a[1];
            if (op=="save") SaveState::instance().save(0); else SaveState::instance().load(0);
            use_save_file=old_use; noremark_save_state=old_remark; savefilename=old_name;
            if (op=="load") {
                frame.clear(); raw_valid=false; raw_lines=0;
                frame_w=frame_h=frame_bpp=0; cap_w=cap_h=0;
            }
            if (!reference_state_ok) throw std::runtime_error("save state failed");
        }
        else if (op=="quit" && a.size()==1) { reply(status()); throw 0; }
        else throw std::runtime_error("unknown command or invalid arguments");
        reply(status());
    } catch (const std::exception &) { reply("{\"ok\":false,\"error\":\"invalid command, range, mode or file operation\"}\n"); }
}
}
bool REF_Enabled() { return getenv("DOSBOX_REF_SOCKET") != nullptr; }
void REF_Frame(const void *pixels, unsigned w, unsigned h, unsigned bpp,
               unsigned pitch, const void *pal) {
    if (!REF_Enabled()) return;
    ++frame_count; frame_w=w; frame_h=h; frame_bpp=bpp;
    if (w==640 && h==480 && bpp==32 && raw_valid) {
        frame=raw_frame; memcpy(palette,raw_palette,sizeof(palette));
        cap_w=640; cap_h=480; cap_rgb=false;
        raw_valid=false; raw_lines=0; return;
    }
    raw_valid=false; raw_lines=0;
    if (!w || !h || w>4096 || h>4096 || (bpp!=8 && bpp!=32)) { frame.clear(); cap_w=cap_h=0; return; }
    const auto *src=static_cast<const uint8_t*>(pixels);
    if (bpp==8) {
        frame.resize(size_t(w)*h);
        for (unsigned y=0;y<h;++y) memcpy(frame.data()+size_t(y)*w,src+size_t(y)*pitch,w);
        for (unsigned i=0;i<256;++i) {
            const uint8_t *c=static_cast<const uint8_t*>(pal)+4*i;
            palette[i]={c[0],c[1],c[2]};
        }
        cap_rgb=false;
    } else {
        frame.resize(size_t(w)*h*3);
        for (unsigned y=0;y<h;++y) {
            const auto *row=reinterpret_cast<const uint32_t*>(src+size_t(y)*pitch);
            for (unsigned x=0;x<w;++x) {
                uint32_t c=row[x]; uint8_t *d=frame.data()+(size_t(y)*w+x)*3;
                d[0]=uint8_t((c&GFX_Rmask)>>GFX_Rshift); d[1]=uint8_t((c&GFX_Gmask)>>GFX_Gshift); d[2]=uint8_t((c&GFX_Bmask)>>GFX_Bshift);
            }
        }
        cap_rgb=true;
    }
    cap_w=w; cap_h=h;
}
void REF_RawLine(const uint8_t *base, unsigned start, unsigned mask, unsigned y,
                 unsigned w, unsigned h, const uint32_t *colors) {
    if (!REF_Enabled() || w!=640 || h!=480 || y>=480) return;
    for (unsigned x=0;x<640;++x) raw_frame[y*640+x]=base[(start+x)&mask];
    if (y==0) { raw_lines=0; raw_valid=false; }
    if (y==raw_lines) ++raw_lines;
    raw_valid=raw_lines==480;
    for (unsigned i=0;i<256;++i) raw_palette[i]={
        uint8_t((colors[i]&GFX_Rmask)>>GFX_Rshift),
        uint8_t((colors[i]&GFX_Gmask)>>GFX_Gshift),
        uint8_t((colors[i]&GFX_Bmask)>>GFX_Bshift)};
}
void REF_Pump() {
    if (!REF_Enabled()) return;
    if (!io_registered) { IO_RegisterWriteHandler(0xEE, probe_port, IO_MB | IO_MW); io_registered = true; }
    if (listener<0) {
        const char *path=getenv("DOSBOX_REF_SOCKET");
        sockaddr_un addr={}; addr.sun_family=AF_UNIX;
        if (strlen(path)>=sizeof(addr.sun_path)) E_Exit("Reference socket path too long");
        strcpy(addr.sun_path,path);
        listener=socket(AF_UNIX,SOCK_STREAM,0);
        if (listener<0 || bind(listener,reinterpret_cast<sockaddr*>(&addr),sizeof(addr)))
            E_Exit("Cannot bind reference socket (existing paths are never removed)");
        socket_path=path; atexit(cleanup); chmod(path,0600);
        if (listen(listener,1)) E_Exit("Cannot listen on reference socket");
        fcntl(listener,F_SETFL,O_NONBLOCK);
    }
    do {
        if (stepping && ((tick_target && PIC_Ticks>=tick_target) ||
                         (frame_target && frame_count>=frame_target))) {
            stepping=false; paused=true; reply(status());
        }
        if (peer<0) {
            peer=accept(listener,nullptr,nullptr);
            if (peer>=0) {
                int one=1; setsockopt(peer,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));
                timeval timeout={1,0}; setsockopt(peer,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
            }
        }
        if (peer>=0) {
            pollfd p={peer,POLLIN,0};
            if (poll(&p,1,paused?20:0)>0) {
                char buf[4096]; ssize_t n=recv(peer,buf,sizeof(buf),0);
                if (n<=0) disconnect();
                else {
                    pending.append(buf,size_t(n));
                    if (pending.size()>8192) disconnect();
                }
            }
        } else if (paused) poll(nullptr,0,20);
        size_t end;
        while (!stepping && (end=pending.find('\n'))!=std::string::npos) {
            std::string line=pending.substr(0,end); pending.erase(0,end+1);
            command(line);
        }
    } while (paused);
}
