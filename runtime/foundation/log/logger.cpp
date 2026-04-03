#include "runtime/foundation/log/logger.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

namespace common::log {

struct Logger::Impl {
    std::ostream* stdout_stream = &std::cout;
    std::ostream* stderr_stream = &std::cerr;
};

namespace {

struct Field {
    std::string key;
    std::string value;
    bool quoted = false;
};

std::string EscapeTextValue(std::string_view value) {
    std::string escaped;
    const bool quoted = std::any_of(value.begin(), value.end(), [](const char ch) {
        return std::isspace(static_cast<unsigned char>(ch)) || ch == '"' || ch == '\\' || ch == '=';
    });
    escaped.reserve(value.size() + 8 + (quoted ? 2 : 0));
    if (quoted) {
        escaped.push_back('"');
    }
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    if (quoted) {
        escaped.push_back('"');
    }
    return escaped;
}

std::string EscapeJson(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

bool LooksLikeInteger(std::string_view value) {
    if (value.empty()) {
        return false;
    }

    std::size_t index = value.front() == '-' ? 1 : 0;
    if (index == value.size()) {
        return false;
    }

    for (; index < value.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(value[index]))) {
            return false;
        }
    }
    return true;
}

bool ParseStructuredFields(std::string_view message, std::vector<Field>* fields) {
    if (fields == nullptr) {
        return false;
    }
    fields->clear();

    std::size_t cursor = 0;
    while (cursor < message.size()) {
        while (cursor < message.size() && std::isspace(static_cast<unsigned char>(message[cursor]))) {
            ++cursor;
        }
        if (cursor >= message.size()) {
            break;
        }

        const auto key_start = cursor;
        while (cursor < message.size() && message[cursor] != '=' &&
               !std::isspace(static_cast<unsigned char>(message[cursor]))) {
            ++cursor;
        }
        if (cursor >= message.size() || message[cursor] != '=' || cursor == key_start) {
            fields->clear();
            return false;
        }

        Field field;
        field.key = std::string(message.substr(key_start, cursor - key_start));
        ++cursor;

        if (cursor < message.size() && message[cursor] == '"') {
            field.quoted = true;
            ++cursor;
            while (cursor < message.size()) {
                const char current = message[cursor++];
                if (current == '\\') {
                    if (cursor >= message.size()) {
                        fields->clear();
                        return false;
                    }
                    const char escaped = message[cursor++];
                    switch (escaped) {
                    case '\\':
                        field.value.push_back('\\');
                        break;
                    case '"':
                        field.value.push_back('"');
                        break;
                    case 'n':
                        field.value.push_back('\n');
                        break;
                    case 'r':
                        field.value.push_back('\r');
                        break;
                    case 't':
                        field.value.push_back('\t');
                        break;
                    default:
                        field.value.push_back(escaped);
                        break;
                    }
                    continue;
                }
                if (current == '"') {
                    break;
                }
                field.value.push_back(current);
            }
            if (cursor > message.size() ||
                (cursor <= message.size() && (cursor == 0 || message[cursor - 1] != '"'))) {
                fields->clear();
                return false;
            }
        } else {
            const auto value_start = cursor;
            while (cursor < message.size() && !std::isspace(static_cast<unsigned char>(message[cursor]))) {
                ++cursor;
            }
            field.value = std::string(message.substr(value_start, cursor - value_start));
        }

        fields->push_back(std::move(field));
    }

    return !fields->empty();
}

std::string FormatTimestamp(std::chrono::system_clock::time_point timestamp) {
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(timestamp);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(timestamp - seconds).count();
    const std::time_t raw = std::chrono::system_clock::to_time_t(timestamp);

    std::tm utc_time{};
#if defined(_WIN32)
    gmtime_s(&utc_time, &raw);
#else
    gmtime_r(&raw, &utc_time);
#endif

    std::ostringstream output;
    output << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
           << milliseconds << 'Z';
    return output.str();
}

void AppendBaseFields(std::vector<Field>* fields, const Logger::LogRecord& record, std::string_view severity) {
    if (fields == nullptr) {
        return;
    }

    fields->push_back({"timestamp", FormatTimestamp(record.timestamp), true});
    fields->push_back({"severity", std::string(severity), true});
    fields->push_back({"service.name", record.service_name, true});
    if (!record.service_instance_id.empty()) {
        fields->push_back({"service.instance.id", record.service_instance_id, true});
    }
    if (!record.environment.empty()) {
        fields->push_back({"deployment.environment", record.environment, true});
    }
}

std::string FormatTextField(const Field& field) {
    if (!field.quoted &&
        (field.value == "true" || field.value == "false" || field.value == "null" || LooksLikeInteger(field.value))) {
        return field.key + '=' + field.value;
    }
    return field.key + '=' + EscapeTextValue(field.value);
}

std::string FormatTextLine(const Logger::LogRecord& record, std::string_view severity) {
    std::vector<Field> fields;
    AppendBaseFields(&fields, record, severity);

    std::vector<Field> structured_fields;
    if (ParseStructuredFields(record.message, &structured_fields)) {
        fields.insert(fields.end(), structured_fields.begin(), structured_fields.end());
    } else {
        fields.push_back({"message", record.message, true});
    }

    std::ostringstream output;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0) {
            output << ' ';
        }
        output << FormatTextField(fields[index]);
    }
    return output.str();
}

std::string JsonValue(const Field& field) {
    if (!field.quoted &&
        (field.value == "true" || field.value == "false" || field.value == "null" || LooksLikeInteger(field.value))) {
        return field.value;
    }
    return '"' + EscapeJson(field.value) + '"';
}

std::string FormatJsonLine(const Logger::LogRecord& record, std::string_view severity) {
    std::vector<Field> fields;
    AppendBaseFields(&fields, record, severity);

    std::vector<Field> structured_fields;
    if (ParseStructuredFields(record.message, &structured_fields)) {
        fields.insert(fields.end(), structured_fields.begin(), structured_fields.end());
    } else {
        fields.push_back({"message", record.message, true});
    }

    std::ostringstream output;
    output << '{';
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << '"' << EscapeJson(fields[index].key) << "\":" << JsonValue(fields[index]);
    }
    output << '}';
    return output.str();
}

bool TryParseLogLevel(std::string_view level_name, LogLevel* level) {
    if (level == nullptr) {
        return false;
    }

    std::string lowered(level_name);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (lowered == "debug") {
        *level = LogLevel::kDebug;
        return true;
    }
    if (lowered == "info") {
        *level = LogLevel::kInfo;
        return true;
    }
    if (lowered == "warn" || lowered == "warning") {
        *level = LogLevel::kWarn;
        return true;
    }
    if (lowered == "error") {
        *level = LogLevel::kError;
        return true;
    }
    return false;
}

bool TryParseLogFormat(std::string_view format_name, LogFormat* format) {
    if (format == nullptr) {
        return false;
    }

    std::string lowered(format_name);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (lowered == "auto") {
        *format = LogFormat::kAuto;
        return true;
    }
    if (lowered == "text") {
        *format = LogFormat::kText;
        return true;
    }
    if (lowered == "json") {
        *format = LogFormat::kJson;
        return true;
    }
    return false;
}

}  // namespace

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() : impl_(std::make_unique<Impl>()) {
}

Logger::~Logger() {
    Shutdown();
}

void Logger::SetServiceName(std::string service_name) {
    std::lock_guard lock(metadata_mutex_);
    service_name_ = std::move(service_name);
}

void Logger::SetServiceInstanceId(std::string service_instance_id) {
    std::lock_guard lock(metadata_mutex_);
    service_instance_id_ = std::move(service_instance_id);
}

void Logger::SetEnvironment(std::string environment) {
    std::lock_guard lock(metadata_mutex_);
    environment_ = std::move(environment);
}

void Logger::SetMinLogLevel(LogLevel level) {
    std::lock_guard lock(metadata_mutex_);
    min_log_level_ = level;
}

bool Logger::SetMinLogLevel(std::string_view level_name) {
    LogLevel level = LogLevel::kInfo;
    if (!TryParseLogLevel(level_name, &level)) {
        return false;
    }
    SetMinLogLevel(level);
    return true;
}

void Logger::SetLogFormat(LogFormat format) {
    std::lock_guard lock(metadata_mutex_);
    log_format_ = format;
}

bool Logger::SetLogFormat(std::string_view format_name) {
    LogFormat format = LogFormat::kAuto;
    if (!TryParseLogFormat(format_name, &format)) {
        return false;
    }
    SetLogFormat(format);
    return true;
}

void Logger::Log(LogLevel level, std::string_view message) {
    if (!ShouldLog(level)) {
        return;
    }
    WriteRecord(BuildRecord(level, message));
}

void Logger::LogSync(LogLevel level, std::string_view message) {
    Log(level, message);
}

void Logger::Flush() {
    if (impl_ != nullptr) {
        impl_->stdout_stream->flush();
        impl_->stderr_stream->flush();
    }
}

void Logger::Shutdown() {
    Flush();
}

std::string Logger::LevelToString(LogLevel level) const {
    switch (level) {
    case LogLevel::kDebug:
        return "DEBUG";
    case LogLevel::kInfo:
        return "INFO";
    case LogLevel::kWarn:
        return "WARN";
    case LogLevel::kError:
        return "ERROR";
    }

    return "UNKNOWN";
}

Logger::LogRecord Logger::BuildRecord(LogLevel level, std::string_view message) const {
    std::lock_guard lock(metadata_mutex_);

    LogRecord record;
    record.timestamp = std::chrono::system_clock::now();
    record.level = level;
    record.service_name = service_name_;
    record.service_instance_id = service_instance_id_.empty() ? service_name_ : service_instance_id_;
    record.environment = environment_;
    record.format = ResolveLogFormat(environment_);
    record.message = std::string(message);
    return record;
}

LogLevel Logger::CurrentMinLogLevel() const {
    std::lock_guard lock(metadata_mutex_);
    return min_log_level_;
}

bool Logger::ShouldLog(LogLevel level) const {
    return static_cast<int>(level) >= static_cast<int>(CurrentMinLogLevel());
}

LogFormat Logger::ResolveLogFormat(std::string_view environment) const {
    if (log_format_ == LogFormat::kJson || log_format_ == LogFormat::kText) {
        return log_format_;
    }
    return environment == "prod" ? LogFormat::kJson : LogFormat::kText;
}

void Logger::WriteRecord(LogRecord record) {
    const auto line =
        record.format == LogFormat::kJson ? FormatJsonLine(record, LevelToString(record.level))
                                          : FormatTextLine(record, LevelToString(record.level));

    std::lock_guard lock(output_mutex_);
    auto* stream = record.level == LogLevel::kWarn || record.level == LogLevel::kError ? impl_->stderr_stream
                                                                                        : impl_->stdout_stream;
    (*stream) << line << '\n';
    stream->flush();
}

}  // namespace common::log
