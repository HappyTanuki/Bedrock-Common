#include "common/archive/transcriber/yaml_deserializer.h"

#include <limits>

#include "common/archive/transcriber/transciber.h"

namespace bedrock::archive::transcriber {

YAMLDeserializer::~YAMLDeserializer() = default;

void YAMLDeserializer::OnRootBegin(std::string_view name) { return; }
void YAMLDeserializer::OnRootEnd() { return; }

void YAMLDeserializer::OnObjectBegin(std::string_view name) { return; }
void YAMLDeserializer::OnObjectEnd() { return; }

std::size_t YAMLDeserializer::OnSeqBegin(std::string_view name,
                                         std::size_t count) {
  return std::numeric_limits<std::size_t>::max();
}
void YAMLDeserializer::OnSeqEnd() { return; }

std::size_t YAMLDeserializer::OnMapBegin(std::string_view name,
                                         std::size_t count) {
  return std::numeric_limits<std::size_t>::max();
}
void YAMLDeserializer::OnMapEnd() { return; }

template <typename T>
static Status VisitImpl(std::string_view name, T& value, const Status& status,
                        std::stack<Object>& _objects) {
  if (status.failed()) {
    return status;
  }
  return status;
}

YAMLDeserializer& YAMLDeserializer::Visit(std::string_view name, bool& value) {
  status = VisitImpl(name, value, status, _objects);
  return *this;
}
YAMLDeserializer& YAMLDeserializer::Visit(std::string_view name,
                                          std::byte& value) {
  status = VisitImpl(name, value, status, _objects);
  return *this;
}
YAMLDeserializer& YAMLDeserializer::Visit(std::string_view name,
                                          std::int8_t& value) {
  status = VisitImpl(name, value, status, _objects);
  return *this;
}
YAMLDeserializer& YAMLDeserializer::Visit(std::string_view name,
                                          std::uint8_t& value) {
  status = VisitImpl(name, value, status, _objects);
  return *this;
}
YAMLDeserializer& YAMLDeserializer::Visit(std::string_view name,
                                          std::int16_t& value) {
  status = VisitImpl(name, value, status, _objects);
  return *this;
}
YAMLDeserializer& YAMLDeserializer::Visit(std::string_view name,
                                          std::uint16_t& value) {
  status = VisitImpl(name, value, status, _objects);
  return *this;
}
YAMLDeserializer& YAMLDeserializer::Visit(std::string_view name,
                                          std::int32_t& value) {
  status = VisitImpl(name, value, status, _objects);
  return *this;
}
YAMLDeserializer& YAMLDeserializer::Visit(std::string_view name,
                                          std::uint32_t& value) {
  status = VisitImpl(name, value, status, _objects);
  return *this;
}
YAMLDeserializer& YAMLDeserializer::Visit(std::string_view name,
                                          std::int64_t& value) {
  status = VisitImpl(name, value, status, _objects);
  return *this;
}
YAMLDeserializer& YAMLDeserializer::Visit(std::string_view name,
                                          std::uint64_t& value) {
  status = VisitImpl(name, value, status, _objects);
  return *this;
}
YAMLDeserializer& YAMLDeserializer::Visit(std::string_view name, float& value) {
  status = VisitImpl(name, value, status, _objects);
  return *this;
}
YAMLDeserializer& YAMLDeserializer::Visit(std::string_view name,
                                          double& value) {
  status = VisitImpl(name, value, status, _objects);
  return *this;
}
YAMLDeserializer& YAMLDeserializer::Visit(std::string_view name,
                                          std::vector<std::byte>& value) {
  status = VisitImpl(name, value, status, _objects);
  return *this;
}
YAMLDeserializer& YAMLDeserializer::Visit(std::string_view name,
                                          std::string& value) {
  status = VisitImpl(name, value, status, _objects);
  return *this;
}

}  // namespace bedrock::archive::transcriber
