#include "nosedive/profile.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace nosedive {

// --- Minimal JSON parser (no external dependencies) ---
// Handles the flat/nested structure of board profile JSON files.

namespace json {

struct Token {
    enum Type { String, Number, LBrace, RBrace, LBracket, RBracket, Colon, Comma, True, False, Null, End };
    Type type;
    std::string value;
};

class Lexer {
public:
    explicit Lexer(const std::string& input) : input_(input), pos_(0) {}

    Token next() {
        skip_whitespace();
        if (pos_ >= input_.size()) return {Token::End, ""};

        char c = input_[pos_];
        switch (c) {
            case '{': pos_++; return {Token::LBrace, "{"};
            case '}': pos_++; return {Token::RBrace, "}"};
            case '[': pos_++; return {Token::LBracket, "["};
            case ']': pos_++; return {Token::RBracket, "]"};
            case ':': pos_++; return {Token::Colon, ":"};
            case ',': pos_++; return {Token::Comma, ","};
            case '"': return read_string();
            case 't': return read_literal("true", Token::True);
            case 'f': return read_literal("false", Token::False);
            case 'n': return read_literal("null", Token::Null);
            default:
                if (c == '-' || std::isdigit(c)) return read_number();
                pos_++; // skip unknown
                return {Token::End, ""};
        }
    }

private:
    void skip_whitespace() {
        while (pos_ < input_.size() && std::isspace(input_[pos_])) pos_++;
    }

    Token read_string() {
        pos_++; // skip opening quote
        std::string s;
        while (pos_ < input_.size() && input_[pos_] != '"') {
            if (input_[pos_] == '\\' && pos_ + 1 < input_.size()) {
                pos_++;
                switch (input_[pos_]) {
                    case '"': s += '"'; break;
                    case '\\': s += '\\'; break;
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case '/': s += '/'; break;
                    default: s += input_[pos_]; break;
                }
            } else {
                s += input_[pos_];
            }
            pos_++;
        }
        if (pos_ < input_.size()) pos_++; // skip closing quote
        return {Token::String, s};
    }

    Token read_number() {
        size_t start = pos_;
        if (input_[pos_] == '-') pos_++;
        while (pos_ < input_.size() && (std::isdigit(input_[pos_]) || input_[pos_] == '.' || input_[pos_] == 'e' || input_[pos_] == 'E' || input_[pos_] == '+' || input_[pos_] == '-')) {
            if ((input_[pos_] == '-' || input_[pos_] == '+') && pos_ > start + 1 && input_[pos_-1] != 'e' && input_[pos_-1] != 'E') break;
            pos_++;
        }
        return {Token::Number, input_.substr(start, pos_ - start)};
    }

    Token read_literal(const char* expected, Token::Type type) {
        size_t len = std::strlen(expected);
        if (pos_ + len <= input_.size() && input_.substr(pos_, len) == expected) {
            pos_ += len;
            return {type, expected};
        }
        pos_++;
        return {Token::End, ""};
    }

    const std::string& input_;
    size_t pos_;
};

// Simple JSON value representation
struct Value {
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    std::string str_val;
    double num_val = 0;
    bool bool_val = false;
    std::vector<Value> arr_val;
    std::vector<std::pair<std::string, Value>> obj_val;

    const Value* get(const std::string& key) const {
        for (auto& [k, v] : obj_val) {
            if (k == key) return &v;
        }
        return nullptr;
    }

    std::string as_string(const std::string& def = "") const {
        return type == String ? str_val : def;
    }

    double as_double(double def = 0) const {
        return type == Number ? num_val : def;
    }

    int as_int(int def = 0) const {
        return type == Number ? static_cast<int>(num_val) : def;
    }

    bool as_bool(bool def = false) const {
        return type == Bool ? bool_val : def;
    }
};

class Parser {
public:
    explicit Parser(const std::string& input) : lex_(input), cur_(lex_.next()) {}

    Value parse() { return parse_value(); }

private:
    Token advance() { auto t = cur_; cur_ = lex_.next(); return t; }

    Value parse_value() {
        switch (cur_.type) {
            case Token::String: { Value v; v.type = Value::String; v.str_val = cur_.value; advance(); return v; }
            case Token::Number: { Value v; v.type = Value::Number; v.num_val = std::stod(cur_.value); advance(); return v; }
            case Token::True:   { Value v; v.type = Value::Bool; v.bool_val = true; advance(); return v; }
            case Token::False:  { Value v; v.type = Value::Bool; v.bool_val = false; advance(); return v; }
            case Token::Null:   { Value v; advance(); return v; }
            case Token::LBrace: return parse_object();
            case Token::LBracket: return parse_array();
            default: { advance(); return Value{}; }
        }
    }

    Value parse_object() {
        Value v;
        v.type = Value::Object;
        advance(); // skip {
        while (cur_.type != Token::RBrace && cur_.type != Token::End) {
            if (cur_.type != Token::String) { advance(); continue; }
            std::string key = cur_.value;
            advance(); // key
            if (cur_.type == Token::Colon) advance(); // :
            v.obj_val.emplace_back(key, parse_value());
            if (cur_.type == Token::Comma) advance();
        }
        if (cur_.type == Token::RBrace) advance();
        return v;
    }

    Value parse_array() {
        Value v;
        v.type = Value::Array;
        advance(); // skip [
        while (cur_.type != Token::RBracket && cur_.type != Token::End) {
            v.arr_val.push_back(parse_value());
            if (cur_.type == Token::Comma) advance();
        }
        if (cur_.type == Token::RBracket) advance();
        return v;
    }

    Lexer lex_;
    Token cur_;
};

} // namespace json

// --- Profile helpers ---

double Profile::erpm_per_mps() const {
    if (wheel.circumference_m == 0) return 0;
    return static_cast<double>(motor.pole_pairs) * 60.0 / wheel.circumference_m;
}

double Profile::speed_from_erpm(double erpm) const {
    double e = erpm_per_mps();
    return e == 0 ? 0 : erpm / e;
}

double Profile::erpm_from_speed(double mps) const {
    return mps * erpm_per_mps();
}

double Profile::battery_percentage(double voltage) const {
    if (voltage >= battery.voltage_max) return 100.0;
    if (voltage <= battery.voltage_min) return 0.0;
    return (voltage - battery.voltage_min) / (battery.voltage_max - battery.voltage_min) * 100.0;
}

// --- JSON → Profile ---

static Controller parse_controller(const json::Value& v) {
    Controller c;
    if (auto* p = v.get("type")) c.type = p->as_string();
    if (auto* p = v.get("hardware")) c.hardware = p->as_string();
    if (auto* fw = v.get("firmware")) {
        if (auto* p = fw->get("major")) c.firmware.major = static_cast<uint8_t>(p->as_int());
        if (auto* p = fw->get("minor")) c.firmware.minor = static_cast<uint8_t>(p->as_int());
    }
    if (auto* p = v.get("maxCurrent")) c.max_current = p->as_double();
    if (auto* p = v.get("maxBrakeCurrent")) c.max_brake_current = p->as_double();
    return c;
}

static Motor parse_motor(const json::Value& v) {
    Motor m;
    if (auto* p = v.get("type")) m.type = p->as_string();
    if (auto* p = v.get("name")) m.name = p->as_string();
    if (auto* p = v.get("notes")) m.notes = p->as_string();
    if (auto* p = v.get("polePairs")) m.pole_pairs = p->as_int();
    if (auto* p = v.get("resistance")) m.resistance = p->as_double();
    if (auto* p = v.get("inductance")) m.inductance = p->as_double();
    if (auto* p = v.get("fluxLinkage")) m.flux_linkage = p->as_double();
    if (auto* p = v.get("maxCurrent")) m.max_current = p->as_double();
    if (auto* p = v.get("maxBrakeCurrent")) m.max_brake_current = p->as_double();
    if (auto* p = v.get("kv")) m.kv = p->as_double();
    if (auto* p = v.get("hallSensorTable")) {
        for (auto& elem : p->arr_val) {
            m.hall_sensor_table.push_back(elem.as_int());
        }
    }
    return m;
}

static Battery parse_battery(const json::Value& v) {
    Battery b;
    if (auto* p = v.get("chemistry")) b.chemistry = p->as_string();
    if (auto* p = v.get("cellType")) b.cell_type = p->as_string();
    if (auto* p = v.get("configuration")) b.configuration = p->as_string();
    if (auto* p = v.get("seriesCells")) b.series_cells = p->as_int();
    if (auto* p = v.get("parallelCells")) b.parallel_cells = p->as_int();
    if (auto* p = v.get("capacityAh")) b.capacity_ah = p->as_double();
    if (auto* p = v.get("capacityWh")) b.capacity_wh = p->as_double();
    if (auto* p = v.get("voltageMin")) b.voltage_min = p->as_double();
    if (auto* p = v.get("voltageNominal")) b.voltage_nominal = p->as_double();
    if (auto* p = v.get("voltageMax")) b.voltage_max = p->as_double();
    if (auto* p = v.get("cutoffStart")) b.cutoff_start = p->as_double();
    if (auto* p = v.get("cutoffEnd")) b.cutoff_end = p->as_double();
    if (auto* p = v.get("maxDischargeCurrent")) b.max_discharge_current = p->as_double();
    if (auto* p = v.get("maxChargeCurrent")) b.max_charge_current = p->as_double();
    if (auto* p = v.get("cellMinVoltage")) b.cell_min_voltage = p->as_double();
    if (auto* p = v.get("cellMaxVoltage")) b.cell_max_voltage = p->as_double();
    if (auto* p = v.get("cellNominalVoltage")) b.cell_nominal_voltage = p->as_double();
    return b;
}

static Wheel parse_wheel(const json::Value& v) {
    Wheel w;
    if (auto* p = v.get("diameter")) w.diameter = p->as_double();
    if (auto* p = v.get("diameterUnit")) w.diameter_unit = p->as_string();
    if (auto* p = v.get("tirePressurePSI")) w.tire_pressure_psi = p->as_double();
    if (auto* p = v.get("circumferenceM")) w.circumference_m = p->as_double();
    return w;
}

static Performance parse_performance(const json::Value& v) {
    Performance perf;
    if (auto* p = v.get("topSpeedMPH")) perf.top_speed_mph = p->as_double();
    if (auto* p = v.get("rangeMiles")) perf.range_miles = p->as_double();
    if (auto* p = v.get("weightLbs")) perf.weight_lbs = p->as_double();
    return perf;
}

std::optional<Profile> load_profile(const std::string& json_str) {
    json::Parser parser(json_str);
    auto root = parser.parse();
    if (root.type != json::Value::Object) return std::nullopt;

    Profile p;
    if (auto* v = root.get("name")) p.name = v->as_string();
    if (auto* v = root.get("manufacturer")) p.manufacturer = v->as_string();
    if (auto* v = root.get("model")) p.model = v->as_string();
    if (auto* v = root.get("description")) p.description = v->as_string();
    if (auto* v = root.get("controller")) p.controller = parse_controller(*v);
    if (auto* v = root.get("motor")) p.motor = parse_motor(*v);
    if (auto* v = root.get("battery")) p.battery = parse_battery(*v);
    if (auto* v = root.get("wheel")) p.wheel = parse_wheel(*v);
    if (auto* v = root.get("performance")) p.performance = parse_performance(*v);
    return p;
}

std::optional<Profile> load_profile_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return load_profile(ss.str());
}

} // namespace nosedive
