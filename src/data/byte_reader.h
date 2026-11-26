#pragma once
#include "data/result.h"
#include <cstdint>
namespace mtv {
inline bool checked_range(size_t size,size_t offset,size_t length){return offset<=size && length<=size-offset;}
class ByteReader {
 ByteView bytes_; size_t pos_{}; std::string file_,resource_;
public:
 ByteReader(ByteView b,std::string file={},std::string resource={}):bytes_(b),file_(std::move(file)),resource_(std::move(resource)){}
 size_t position()const{return pos_;}
 size_t remaining()const{return bytes_.size()-pos_;}
 Result<ByteView> take(size_t n);
 Result<uint8_t> u8(); Result<uint16_t> u16le(); Result<uint32_t> u32le();
 DataError error(std::string why)const{return {file_,resource_,pos_,std::move(why)};}
};
}
