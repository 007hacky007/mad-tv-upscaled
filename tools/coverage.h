#pragma once
#include "data/rsc.h"
#include "data/rsc_codec.h"
#include <map>
#include <string>
#include <vector>
namespace mtv {
struct CoverageEntry { uint16_t slot; std::string archive; ResourceKind kind; bool decoded; std::string note; };
struct CoverageSummary { bool complete = false; int unknown = 0, failed = 0; std::map<std::string, int> by_kind; };
const char* kind_name(ResourceKind k);
// Kind of a slot from code evidence (analysis/rsc-map.json) where decoding alone cannot tell.
ResourceKind slot_kind(uint16_t slot, ResourceKind decoded_kind);
std::vector<CoverageEntry> build_coverage(const ResourceIndex& index);
CoverageSummary summarize_coverage(const std::vector<CoverageEntry>& entries);
}
