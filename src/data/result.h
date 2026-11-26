#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <variant>
namespace mtv {
using ByteView = std::span<const std::byte>;
struct DataError { std::string file, resource; std::size_t byte_offset{}; std::string reason; };
template<class T> class Result {
    std::variant<T, DataError> data_;
public:
    Result(T value) : data_(std::move(value)) {}
    Result(DataError error) : data_(std::move(error)) {}
    bool has_value() const { return std::holds_alternative<T>(data_); }
    T& value() { return std::get<T>(data_); }
    const T& value() const { return std::get<T>(data_); }
    const DataError& error() const { return std::get<DataError>(data_); }
};
template<> class Result<void> {
    std::variant<std::monostate, DataError> data_;
public:
    Result() = default;
    Result(DataError error) : data_(std::move(error)) {}
    bool has_value() const { return std::holds_alternative<std::monostate>(data_); }
    const DataError& error() const { return std::get<DataError>(data_); }
};
}
