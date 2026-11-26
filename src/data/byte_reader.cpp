#include "data/byte_reader.h"
namespace mtv {
Result<ByteView> ByteReader::take(size_t n){if(!checked_range(bytes_.size(),pos_,n))return error("truncated byte range");auto out=bytes_.subspan(pos_,n);pos_+=n;return out;}
Result<uint8_t> ByteReader::u8(){auto b=take(1);if(!b.has_value())return b.error();return std::to_integer<uint8_t>(b.value()[0]);}
Result<uint16_t> ByteReader::u16le(){auto b=take(2);if(!b.has_value())return b.error();return uint16_t(std::to_integer<uint8_t>(b.value()[0])|(std::to_integer<uint8_t>(b.value()[1])<<8));}
Result<uint32_t> ByteReader::u32le(){auto b=take(4);if(!b.has_value())return b.error();uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(std::to_integer<uint8_t>(b.value()[i]))<<(8*i);return v;}
}
