#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <simdjson.h>

namespace detersl::fastjson {

struct Value;

using Array = std::vector<Value>;
using Object = std::unordered_map<std::string, Value>;

struct Value {
  enum class Type : uint8_t {
    Null,
    Bool,
    Int64,
    UInt64,
    Double,
    String,
    Array,
    Object,
  };

  using Storage = std::variant<std::monostate, bool, int64_t, uint64_t, double, std::string, Array, Object>;

  Value() = default;
  Value(std::nullptr_t) {}
  Value(bool value) : storage_(value) {}
  Value(int value) : storage_(static_cast<int64_t>(value)) {}
  Value(int64_t value) : storage_(value) {}
  Value(uint64_t value) : storage_(value) {}
  Value(double value) : storage_(value) {}
  Value(const char* value) : storage_(std::string(value)) {}
  Value(std::string value) : storage_(std::move(value)) {}
  Value(std::string_view value) : storage_(std::string(value)) {}
  Value(Array value) : storage_(std::move(value)) {}
  Value(Object value) : storage_(std::move(value)) {}

  Type type() const {
    switch (storage_.index()) {
      case 0:
        return Type::Null;
      case 1:
        return Type::Bool;
      case 2:
        return Type::Int64;
      case 3:
        return Type::UInt64;
      case 4:
        return Type::Double;
      case 5:
        return Type::String;
      case 6:
        return Type::Array;
      case 7:
        return Type::Object;
      default:
        throw std::logic_error("invalid fastjson::Value storage index");
    }
  }

  bool is_null() const { return std::holds_alternative<std::monostate>(storage_); }
  bool is_bool() const { return std::holds_alternative<bool>(storage_); }
  bool is_int64() const { return std::holds_alternative<int64_t>(storage_); }
  bool is_uint64() const { return std::holds_alternative<uint64_t>(storage_); }
  bool is_double() const { return std::holds_alternative<double>(storage_); }
  bool is_number() const { return is_int64() || is_uint64() || is_double(); }
  bool is_string() const { return std::holds_alternative<std::string>(storage_); }
  bool is_array() const { return std::holds_alternative<Array>(storage_); }
  bool is_object() const { return std::holds_alternative<Object>(storage_); }

  bool get_bool() const { return std::get<bool>(storage_); }
  int64_t get_int64() const { return std::get<int64_t>(storage_); }
  uint64_t get_uint64() const { return std::get<uint64_t>(storage_); }
  double get_double() const { return std::get<double>(storage_); }
  const std::string& get_string() const { return std::get<std::string>(storage_); }
  const Array& get_array() const { return std::get<Array>(storage_); }
  const Object& get_object() const { return std::get<Object>(storage_); }

  std::string dump() const;

 private:
  Storage storage_;
};

struct InputField {
  enum class Kind : uint8_t {
    Null,
    Bool,
    Int64,
    UInt64,
    Double,
    String,
    Array,
    Object,
  };

  Kind kind = Kind::Null;
  std::string raw_json;
  std::string string_value;
  bool bool_value = false;
  int64_t int64_value = 0;
  uint64_t uint64_value = 0;
  double double_value = 0.0;

  bool is_null() const { return kind == Kind::Null; }
  bool is_bool() const { return kind == Kind::Bool; }
  bool is_int64() const { return kind == Kind::Int64; }
  bool is_uint64() const { return kind == Kind::UInt64; }
  bool is_double() const { return kind == Kind::Double; }
  bool is_number() const { return is_int64() || is_uint64() || is_double(); }
  bool is_string() const { return kind == Kind::String; }
  bool is_array() const { return kind == Kind::Array; }
  bool is_object() const { return kind == Kind::Object; }

  bool get_bool() const { return bool_value; }
  int64_t get_int64() const { return int64_value; }
  uint64_t get_uint64() const { return uint64_value; }
  double get_double() const { return double_value; }
  const std::string& get_string() const { return string_value; }

  std::string_view raw_view() const { return raw_json; }

  bool get_string_array(const std::vector<std::string>** out, std::string* err) const;

 private:
  bool materialize_string_array(std::string* err) const;

  mutable bool string_array_cached_ = false;
  mutable bool string_array_valid_ = false;
  mutable std::vector<std::string> string_array_;
};

using InputObject = std::unordered_map<std::string, InputField>;
using ValueInputs = std::unordered_map<std::string, InputField>;

struct ResourceValue {
  enum class Kind : uint8_t {
    String,
    StringArray,
  };

  Kind kind = Kind::String;
  std::string string_value;
  std::vector<std::string> string_array;

  ResourceValue() = default;
  ResourceValue(std::string value) : kind(Kind::String), string_value(std::move(value)) {}
  ResourceValue(std::vector<std::string> value) : kind(Kind::StringArray), string_array(std::move(value)) {}

  bool is_string() const { return kind == Kind::String; }
  bool is_string_array() const { return kind == Kind::StringArray; }
  const std::string& get_string() const { return string_value; }
  const std::vector<std::string>& get_string_array() const { return string_array; }
};

using ResourceInputs = std::unordered_map<std::string, ResourceValue>;

struct InvokeRequest {
  std::string WorkflowID;
  bool can_abort = true;
  InputObject Input;
};

inline void append_value(simdjson::builder::string_builder& builder, const Value& value);

inline void append_object(simdjson::builder::string_builder& builder, const Object& object) {
  builder.start_object();
  bool first = true;
  for (const auto& entry : object) {
    if (!first) {
      builder.append_comma();
    }
    first = false;
    builder.escape_and_append_with_quotes(entry.first);
    builder.append_colon();
    append_value(builder, entry.second);
  }
  builder.end_object();
}

inline void append_array(simdjson::builder::string_builder& builder, const Array& array) {
  builder.start_array();
  bool first = true;
  for (const auto& entry : array) {
    if (!first) {
      builder.append_comma();
    }
    first = false;
    append_value(builder, entry);
  }
  builder.end_array();
}

inline void append_value(simdjson::builder::string_builder& builder, const Value& value) {
  switch (value.type()) {
    case Value::Type::Null:
      builder.append_null();
      return;
    case Value::Type::Bool:
      builder.append(value.get_bool());
      return;
    case Value::Type::Int64:
      builder.append(value.get_int64());
      return;
    case Value::Type::UInt64:
      builder.append(value.get_uint64());
      return;
    case Value::Type::Double:
      builder.append(value.get_double());
      return;
    case Value::Type::String:
      builder.escape_and_append_with_quotes(value.get_string());
      return;
    case Value::Type::Array:
      append_array(builder, value.get_array());
      return;
    case Value::Type::Object:
      append_object(builder, value.get_object());
      return;
  }
}

inline std::string Value::dump() const {
  simdjson::builder::string_builder builder;
  append_value(builder, *this);
  std::string_view out;
  if (auto error = builder.view().get(out); error) {
    throw std::runtime_error(simdjson::error_message(error));
  }
  return std::string(out);
}

inline simdjson::ondemand::parser& request_parser() {
  static thread_local simdjson::ondemand::parser parser;
  return parser;
}

inline simdjson::ondemand::parser& field_parser() {
  static thread_local simdjson::ondemand::parser parser;
  return parser;
}

inline bool InputField::materialize_string_array(std::string* err) const {
  if (string_array_cached_) {
    if (!string_array_valid_ && err) {
      *err = "input value must be an array of strings";
    }
    return string_array_valid_;
  }

  string_array_cached_ = true;
  string_array_valid_ = false;
  string_array_.clear();

  if (!is_array()) {
    if (err) {
      *err = "input value must be an array of strings";
    }
    return false;
  }

  simdjson::ondemand::document doc;
  simdjson::padded_string padded(raw_json);
  if (auto error = field_parser().iterate(padded).get(doc); error) {
    if (err) {
      *err = std::string("failed to parse array input: ") + simdjson::error_message(error);
    }
    return false;
  }

  simdjson::ondemand::array array;
  if (auto error = doc.get_array().get(array); error) {
    if (err) {
      *err = "input value must be an array of strings";
    }
    return false;
  }

  for (auto item : array) {
    std::string value;
    if (auto error = item.get(value); error) {
      if (err) {
        *err = "input array entries must be strings";
      }
      string_array_.clear();
      return false;
    }
    string_array_.push_back(std::move(value));
  }

  string_array_valid_ = true;
  return true;
}

inline bool InputField::get_string_array(const std::vector<std::string>** out, std::string* err) const {
  if (!materialize_string_array(err)) {
    return false;
  }
  *out = &string_array_;
  return true;
}

inline void append_resource_value(simdjson::builder::string_builder& builder, const ResourceValue& value) {
  if (value.is_string()) {
    builder.escape_and_append_with_quotes(value.get_string());
    return;
  }

  builder.start_array();
  bool first = true;
  for (const auto& entry : value.get_string_array()) {
    if (!first) {
      builder.append_comma();
    }
    first = false;
    builder.escape_and_append_with_quotes(entry);
  }
  builder.end_array();
}

inline std::string dump_task_input(const ResourceInputs& resource_inputs, const ValueInputs& value_inputs) {
  simdjson::builder::string_builder builder;
  builder.start_object();

  builder.escape_and_append_with_quotes("resources");
  builder.append_colon();
  builder.start_object();
  bool first = true;
  for (const auto& entry : resource_inputs) {
    if (!first) {
      builder.append_comma();
    }
    first = false;
    builder.escape_and_append_with_quotes(entry.first);
    builder.append_colon();
    append_resource_value(builder, entry.second);
  }
  builder.end_object();

  builder.append_comma();
  builder.escape_and_append_with_quotes("values");
  builder.append_colon();
  builder.start_object();
  first = true;
  for (const auto& entry : value_inputs) {
    if (!first) {
      builder.append_comma();
    }
    first = false;
    builder.escape_and_append_with_quotes(entry.first);
    builder.append_colon();
    builder.append_raw(entry.second.raw_view());
  }
  builder.end_object();

  builder.end_object();

  std::string_view out;
  if (auto error = builder.view().get(out); error) {
    throw std::runtime_error(simdjson::error_message(error));
  }
  return std::string(out);
}

inline simdjson::error_code copy_raw_json(simdjson::ondemand::value& value, std::string& out) noexcept {
  std::string_view raw;
  SIMDJSON_TRY(value.raw_json().get(raw));
  out.assign(raw);
  return simdjson::SUCCESS;
}

inline simdjson::error_code parse_input_field(simdjson::ondemand::value value, InputField& out) noexcept(false) {
  simdjson::ondemand::json_type type;
  SIMDJSON_TRY(value.type().get(type));

  out = InputField{};
  switch (type) {
    case simdjson::ondemand::json_type::null: {
      bool is_null = false;
      SIMDJSON_TRY(copy_raw_json(value, out.raw_json));
      SIMDJSON_TRY(value.is_null().get(is_null));
      out.kind = InputField::Kind::Null;
      return is_null ? simdjson::SUCCESS : simdjson::INCORRECT_TYPE;
    }
    case simdjson::ondemand::json_type::boolean: {
      SIMDJSON_TRY(copy_raw_json(value, out.raw_json));
      SIMDJSON_TRY(value.get(out.bool_value));
      out.kind = InputField::Kind::Bool;
      return simdjson::SUCCESS;
    }
    case simdjson::ondemand::json_type::number: {
      SIMDJSON_TRY(copy_raw_json(value, out.raw_json));
      simdjson::ondemand::number number;
      SIMDJSON_TRY(value.get_number().get(number));
      switch (number.get_number_type()) {
        case simdjson::ondemand::number_type::signed_integer:
          out.kind = InputField::Kind::Int64;
          out.int64_value = number.get_int64();
          return simdjson::SUCCESS;
        case simdjson::ondemand::number_type::unsigned_integer:
          out.kind = InputField::Kind::UInt64;
          out.uint64_value = number.get_uint64();
          return simdjson::SUCCESS;
        case simdjson::ondemand::number_type::floating_point_number:
          out.kind = InputField::Kind::Double;
          out.double_value = number.get_double();
          return simdjson::SUCCESS;
        case simdjson::ondemand::number_type::big_integer:
          return simdjson::NUMBER_OUT_OF_RANGE;
      }
      return simdjson::INCORRECT_TYPE;
    }
    case simdjson::ondemand::json_type::string: {
      SIMDJSON_TRY(copy_raw_json(value, out.raw_json));
      SIMDJSON_TRY(value.get(out.string_value));
      out.kind = InputField::Kind::String;
      return simdjson::SUCCESS;
    }
    case simdjson::ondemand::json_type::array:
      SIMDJSON_TRY(copy_raw_json(value, out.raw_json));
      out.kind = InputField::Kind::Array;
      return simdjson::SUCCESS;
    case simdjson::ondemand::json_type::object:
      SIMDJSON_TRY(copy_raw_json(value, out.raw_json));
      out.kind = InputField::Kind::Object;
      return simdjson::SUCCESS;
    case simdjson::ondemand::json_type::unknown:
      return simdjson::INCORRECT_TYPE;
  }

  return simdjson::INCORRECT_TYPE;
}

inline InvokeRequest parse_invoke_request(const char* data, size_t size) {
  simdjson::padded_string padded(data, size);
  simdjson::ondemand::document doc;
  if (auto error = request_parser().iterate(padded).get(doc); error) {
    throw std::runtime_error("failed to parse invoke payload: " + std::string(simdjson::error_message(error)));
  }

  simdjson::ondemand::object root;
  if (auto error = doc.get_object().get(root); error) {
    throw std::runtime_error("invoke payload must be a JSON object");
  }

  InvokeRequest request;
  bool saw_workflow_id = false;
  bool saw_input = false;

  for (auto field : root) {
    std::string_view key;
    if (auto error = field.unescaped_key().get(key); error) {
      throw std::runtime_error("failed to decode invoke key: " + std::string(simdjson::error_message(error)));
    }

    auto value = field.value();
    if (key == "workflow_id") {
      if (auto error = value.get(request.WorkflowID); error) {
        throw std::runtime_error("workflow_id must be a JSON string");
      }
      saw_workflow_id = true;
      continue;
    }

    if (key == "can_abort") {
      if (auto error = value.get(request.can_abort); error) {
        throw std::runtime_error("can_abort must be a JSON boolean");
      }
      continue;
    }

    if (key == "input") {
      simdjson::ondemand::object input_object;
      if (auto error = value.get_object().get(input_object); error) {
        throw std::runtime_error("input must be a JSON object");
      }

      for (auto input_field : input_object) {
        std::string_view input_key_view;
        if (auto error = input_field.unescaped_key().get(input_key_view); error) {
          throw std::runtime_error("failed to decode input key: " + std::string(simdjson::error_message(error)));
        }

        InputField parsed;
        simdjson::ondemand::value input_value;
        if (auto error = input_field.value().get(input_value); error) {
          throw std::runtime_error("failed to read input field \"" + std::string(input_key_view) +
                                   "\": " + simdjson::error_message(error));
        }
        if (auto error = parse_input_field(input_value, parsed); error) {
          throw std::runtime_error("failed to parse input field \"" + std::string(input_key_view) +
                                   "\": " + simdjson::error_message(error));
        }

        request.Input.emplace(std::string(input_key_view), std::move(parsed));
      }

      saw_input = true;
    }
  }

  if (!saw_workflow_id) {
    throw std::runtime_error("invoke payload missing required field: workflow_id");
  }
  if (!saw_input) {
    throw std::runtime_error("invoke payload missing required field: input");
  }
  return request;
}

}  // namespace detersl::fastjson
