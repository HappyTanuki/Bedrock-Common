#include "common/archive/transcriber/yaml_serializer.h"

#include <format>
#include <ostream>
#include <string>

#include "common/archive/transcriber/transciber.h"
#include "common/i18n/locales.h"

namespace bedrock::archive::transcriber {

YAMLSerializer::~YAMLSerializer() { Flush(); };

Status YAMLSerializer::Flush() {
  if (_status.failed()) {
    return _status;
  }
  if (!_objects.empty()) {
    std::string fmt(GetI18nString(locale::StringKey::kStatusCorruptedData,
                                  locale::ISO639_1::kKO,
                                  locale::ISO3166_1::kKR));
    return Status(TranscriberError::kCorrupted, fmt);
  }
  if (!_root_object) {
    std::string fmt(GetI18nString(locale::StringKey::kStatusCorruptedData,
                                  locale::ISO639_1::kKO,
                                  locale::ISO3166_1::kKR));
    return Status(TranscriberError::kCorrupted, fmt);
  }
  if (!_output_stream) {
    std::string fmt(GetI18nString(locale::StringKey::kStatusNullStream,
                                  locale::ISO639_1::kKO,
                                  locale::ISO3166_1::kKR));
    return Status(TranscriberError::kCorrupted, fmt);
  }
}

void YAMLSerializer::InsertAppropriateToken(Object& obj) {
  std::size_t indent_level = _objects.size() - 1;
  std::string indent(indent_level * _indent_size, ' ');
  if (obj.object_type == ObjectType::kObject) {
    obj.begin_token = std::format("{}{}:\n", indent, obj.name);
  } else if (obj.object_type == ObjectType::kValue) {
    obj.begin_token = std::format("{}{}: ", indent, obj.name);
  }
}

YAMLSerializer& YAMLSerializer::OnRootBegin(std::string_view name) {
  return OnObjectBegin(name);
}
YAMLSerializer& YAMLSerializer::OnRootEnd() {
  if (_status.failed()) {
    return *this;
  }
  if (_objects.size() != 1) {
    std::string_view fmt =
        GetI18nString(locale::StringKey::kStatusCorruptedWhileParsing,
                      locale::ISO639_1::kKO, locale::ISO3166_1::kKR);
    _status = Status(TranscriberError::kCorrupted,
                     std::vformat(fmt, std::make_format_args("root object")));
    return *this;
  }
  _root_object = std::make_unique<Object>(std::move(_objects.top()));
  _objects.pop();
  return *this;
}

YAMLSerializer& YAMLSerializer::OnObjectBegin(std::string_view name) {
  if (_status.failed()) {
    return *this;
  }
  _objects.push({.id = Snowflake::Generate(_machine_id),
                 .object_type = ObjectType::kObject,
                 .value_type = ValueType::kArray,
                 .name = std::string(name),
                 .begin_token = std::format("{}:\n", name),
                 .children = {},
                 .value = {},
                 .end_token = ""});
  return *this;
}
YAMLSerializer& YAMLSerializer::OnObjectEnd() {
  if (_status.failed()) {
    return *this;
  }
  return *this;
}

template <typename T>
static Status VisitImpl(std::string_view name, T& value,
                        std::uint16_t _machine_id, const Status& _status,
                        std::stack<Object>& _objects) {
  if (_status.failed()) {
    return _status;
  }
  if (_objects.empty()) {
    std::string_view fmt =
        GetI18nString(locale::StringKey::kStatusCorruptedWhileParsing,
                      locale::ISO639_1::kKO, locale::ISO3166_1::kKR);
    return Status(TranscriberError::kCorrupted,
                  std::vformat(fmt, std::make_format_args(name)));
  }
  _objects.top().children.push_back({.id = Snowflake::Generate(_machine_id),
                                     .object_type = ObjectType::kValue,
                                     .value_type = ValueType::kString,
                                     .name = std::string(name),
                                     .begin_token = "",
                                     .children = {},
                                     .value = {},
                                     .end_token = ""});

  std::string value_str;
  if constexpr (std::is_same_v<T, std::byte>) {
    value_str = std::format("{:02X}", std::to_integer<int>(value));
  } else {
    value_str = std::format("{}", value);
  }

  auto bytes_view = std::as_bytes(std::span{value_str});
  _objects.top().value =
      std::vector<std::byte>(bytes_view.begin(), bytes_view.end());
  return _status;
}

YAMLSerializer& YAMLSerializer::Visit(std::string_view name, bool& value) {
  _status = VisitImpl(name, value, _machine_id, _status, _objects);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name, std::byte& value) {
  _status = VisitImpl(name, value, _machine_id, _status, _objects);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::int8_t& value) {
  _status = VisitImpl(name, value, _machine_id, _status, _objects);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::uint8_t& value) {
  _status = VisitImpl(name, value, _machine_id, _status, _objects);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::int16_t& value) {
  _status = VisitImpl(name, value, _machine_id, _status, _objects);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::uint16_t& value) {
  _status = VisitImpl(name, value, _machine_id, _status, _objects);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::int32_t& value) {
  _status = VisitImpl(name, value, _machine_id, _status, _objects);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::uint32_t& value) {
  _status = VisitImpl(name, value, _machine_id, _status, _objects);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::int64_t& value) {
  _status = VisitImpl(name, value, _machine_id, _status, _objects);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::uint64_t& value) {
  _status = VisitImpl(name, value, _machine_id, _status, _objects);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name, float& value) {
  _status = VisitImpl(name, value, _machine_id, _status, _objects);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name, double& value) {
  _status = VisitImpl(name, value, _machine_id, _status, _objects);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::string& value) {
  return *this;
}
}  // namespace bedrock::archive::transcriber
