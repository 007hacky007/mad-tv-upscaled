#pragma once
#include "rt/cpu.h"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
namespace mtv::rt {
using Fn = void (*)(Cpu&);
struct FunctionEntry { uint32_t image_offset; Fn fn; uint16_t cs_paragraph; };
// Services the translated program needs from its environment: interrupts, ports, indirect calls, timer events.
class Machine {
public:
    virtual ~Machine() = default;
    uint16_t load_seg = 0;
    virtual void interrupt(Cpu& c, uint8_t n, uint16_t return_ip) = 0;
    virtual uint8_t in8(Cpu& c, uint16_t port) = 0;
    virtual uint16_t in16(Cpu& c, uint16_t port) { return uint16_t(in8(c, port) | in8(c, uint16_t(port + 1)) << 8); }
    virtual void out8(Cpu& c, uint16_t port, uint8_t v) = 0;
    virtual void out16(Cpu& c, uint16_t port, uint16_t v) { out8(c, port, uint8_t(v)); out8(c, uint16_t(port + 1), uint8_t(v >> 8)); }
    virtual void events(Cpu& c) = 0;                                      // timer interrupts etc. when c.icount >= c.next_event
    virtual void call_near(Cpu& c, uint16_t ip) = 0;                      // return address already pushed
    virtual void call_far(Cpu& c, uint16_t seg, uint16_t off) = 0;        // CS:IP already pushed, c.s[CS] = seg
    virtual void jump_far(Cpu& c, uint16_t seg, uint16_t off) = 0;        // tail transfer; target returns for the caller
    virtual void jump_near(Cpu& c, uint16_t ip) = 0;                      // tail transfer within the current CS
    virtual void hook(Cpu&, uint32_t /*image_offset*/) {}                  // entry of a function named with --hooks
    virtual Tag store_tag(Cpu&, uint32_t /*image_offset*/) { return 0; }   // tag for a store named with --tag-stores
};
}
