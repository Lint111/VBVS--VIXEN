#include "AppFlowBlobFile.h"

#include "Logger.h"
#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace Vixen::AppFlow {
namespace {

Logger& BlobLogger() {
    static Logger logger("AppFlowBlobFile", true);
    return logger;
}

std::optional<uint32_t> Number(std::string_view token, int base = 10) {
    uint32_t value = 0;
    const char* begin = token.data();
    const char* end = token.data() + token.size();
    if (base == 16 && token.starts_with("0x")) begin += 2;
    auto result = std::from_chars(begin, end, value, base);
    if (result.ec != std::errc{} || result.ptr != end) return std::nullopt;
    return value;
}

bool Error(const std::string& message) {
    BlobLogger().Error("appflow: " + message);
    return false;
}

bool ReadU32(std::istringstream& line, uint32_t& out, const char* what) {
    std::string token;
    if (!(line >> token)) return Error(std::string("missing ") + what);
    auto parsed = Number(token);
    if (!parsed) return Error(std::string("bad ") + what + " '" + token + "'");
    out = *parsed;
    return true;
}

bool ReadHex32(std::istringstream& line, uint32_t& out) {
    std::string token;
    if (!(line >> token) || !token.starts_with("0x")) return Error("bad shape hash");
    auto parsed = Number(token, 16);
    if (!parsed) return Error("bad shape hash '" + token + "'");
    out = *parsed;
    return true;
}

bool Footprint(std::string_view token, uint32_t& out) {
    if (auto parsed = Number(token)) { out = *parsed; return true; }
    constexpr std::string_view prefix = "sizeof(";
    if (token.starts_with(prefix) && token.ends_with(')')) {
        const auto type = token.substr(prefix.size(), token.size() - prefix.size() - 1);
        if (type == "LayerState") { out = sizeof(Generated::LayerState); return true; }
    }
    return Error("bad action footprint '" + std::string(token) + "'");
}

template <typename T>
bool Narrow(uint32_t value, T& out, const char* what) {
    if (value > static_cast<uint32_t>(std::numeric_limits<std::underlying_type_t<T>>::max()))
        return Error(std::string(what) + " is out of range");
    out = static_cast<T>(value);
    return true;
}

} // namespace

std::optional<AppFlowBlobFile> AppFlowBlobFile::Parse(std::string_view text) {
    try {
        AppFlowBlobFile file;
        std::istringstream input{std::string(text)};
        std::string raw;
        bool haveModel = false;
        bool haveShape = false;
        struct ActionRow { uint32_t id; uint32_t footprint; bool hasInvert; };
        std::vector<ActionRow> actionRows;
        std::unordered_map<uint32_t, std::vector<Generated::FlowParamSchema>> paramsByAction;

        auto intern = [&](std::string value) -> const char* {
            file.strings_.push_back(std::move(value));
            return file.strings_.back().c_str();
        };

        while (std::getline(input, raw)) {
            if (raw.empty() || raw[0] == '#') continue;
            std::istringstream line(raw);
            std::string directive;
            line >> directive;
            if (directive == "model") {
                std::string model;
                if (!(line >> model) || model != "appflow") { Error("bad model"); return std::nullopt; }
                haveModel = true;
            } else if (directive == "shape") {
                if (!ReadHex32(line, file.shapeHash_)) return std::nullopt;
                haveShape = true;
            } else if (directive == "state" || directive == "guard" || directive == "action" ||
                       directive == "paramtype" || directive == "key" || directive == "mod") {
                std::string name; uint32_t value;
                if (!(line >> name) || !ReadU32(line, value, "enum value")) return std::nullopt;
            } else if (directive == "decl") {
                uint32_t id, hasInvert; std::string footprint;
                if (!ReadU32(line, id, "action id") || !(line >> footprint) ||
                    !ReadU32(line, hasInvert, "hasInvert") || hasInvert > 1) return std::nullopt;
                uint32_t bytes = 0;
                if (!Footprint(footprint, bytes)) return std::nullopt;
                actionRows.push_back({id, bytes, hasInvert != 0});
            } else if (directive == "param") {
                uint32_t action, type; std::string name;
                if (!ReadU32(line, action, "param action id") || !(line >> name) ||
                    !ReadU32(line, type, "param type")) return std::nullopt;
                Generated::FlowParamType paramType;
                if (!Narrow(type, paramType, "param type")) return std::nullopt;
                paramsByAction[action].push_back({intern(name), paramType});
            } else if (directive == "transition") {
                uint32_t from, to, guard; std::string effect;
                if (!ReadU32(line, from, "transition from") || !ReadU32(line, to, "transition to") ||
                    !ReadU32(line, guard, "transition guard") || !(line >> effect)) return std::nullopt;
                Generated::FlowStateId fromId, toId; Generated::FlowGuardId guardId;
                if (!Narrow(from, fromId, "transition from") || !Narrow(to, toId, "transition to") ||
                    !Narrow(guard, guardId, "transition guard")) return std::nullopt;
                file.transitions_.push_back({fromId, toId, guardId, intern(effect)});
            } else if (directive == "trigger") {
                uint32_t action; std::string element, param, on;
                if (!(line >> element) || !ReadU32(line, action, "trigger action") ||
                    !(line >> param >> on)) return std::nullopt;
                if (param == "-") param.clear();
                Generated::FlowActionId actionId;
                if (!Narrow(action, actionId, "trigger action")) return std::nullopt;
                file.elementTriggers_.push_back({intern(element), actionId, intern(param), intern(on)});
            } else if (directive == "keydefault") {
                uint32_t action, key, mods, scope, state;
                if (!ReadU32(line, action, "key action") || !ReadU32(line, key, "key id") ||
                    !ReadU32(line, mods, "key mods") || !ReadU32(line, scope, "key scope") ||
                    !ReadU32(line, state, "key state")) return std::nullopt;
                Generated::FlowActionId actionId; Generated::KeyId keyId; Generated::KeyMod modId;
                Generated::FlowScope scopeId; Generated::FlowStateId stateId;
                if (!Narrow(action, actionId, "key action") || !Narrow(key, keyId, "key id") ||
                    !Narrow(mods, modId, "key mods") || !Narrow(scope, scopeId, "key scope") ||
                    !Narrow(state, stateId, "key state")) return std::nullopt;
                file.keyDefaults_.push_back({actionId, {keyId, modId}, scopeId, stateId});
            } else if (directive == "return") {
                uint32_t from, key, mods;
                if (!ReadU32(line, from, "return state") || !ReadU32(line, key, "return key") ||
                    !ReadU32(line, mods, "return mods")) return std::nullopt;
                Generated::FlowStateId fromId; Generated::KeyId keyId; Generated::KeyMod modId;
                if (!Narrow(from, fromId, "return state") || !Narrow(key, keyId, "return key") ||
                    !Narrow(mods, modId, "return mods")) return std::nullopt;
                file.returnEdges_.push_back({fromId, {keyId, modId}});
            } else if (directive == "data") {
                uint32_t action, ordinal; std::string noun;
                if (!ReadU32(line, action, "data action") || !ReadU32(line, ordinal, "data noun ordinal") ||
                    !(line >> noun)) return std::nullopt;
                Generated::FlowActionId actionId;
                if (!Narrow(action, actionId, "data action")) return std::nullopt;
                file.dataTargets_.push_back({actionId, static_cast<ViewNounId>(ordinal)});
            } else {
                Error("unknown directive '" + directive + "'");
                return std::nullopt;
            }
        }

        if (!haveModel || !haveShape || actionRows.empty()) {
            Error("missing model/shape or empty action table");
            return std::nullopt;
        }
        for (const auto& row : actionRows) {
            auto it = paramsByAction.find(row.id);
            if (it == paramsByAction.end()) file.paramArrays_.emplace_back();
            else file.paramArrays_.push_back(std::move(it->second));
            const auto& params = file.paramArrays_.back();
            Generated::FlowActionId actionId;
            if (!Narrow(row.id, actionId, "action id")) return std::nullopt;
            file.actions_.push_back({actionId, row.footprint, row.hasInvert,
                                     params.empty() ? nullptr : params.data(),
                                     static_cast<uint32_t>(params.size())});
        }

        file.view_ = Generated::AppFlowContainerView(
            file.actions_, file.transitions_, file.elementTriggers_, file.keyDefaults_,
            file.returnEdges_, file.dataTargets_);
        if (file.shapeHash_ != Generated::kAppFlowShapeHash) {
            Error("shape hash mismatch: this facade change alters the interface/graph -> rebuild required");
            return std::nullopt;
        }
        return file;
    } catch (const std::exception& e) {
        Error(std::string("exception while parsing: ") + e.what());
        return std::nullopt;
    } catch (...) {
        Error("unknown exception while parsing");
        return std::nullopt;
    }
}

std::optional<AppFlowBlobFile> AppFlowBlobFile::Load(const std::string& path) {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) { Error("cannot open '" + path + "'"); return std::nullopt; }
        std::ostringstream contents;
        contents << input.rdbuf();
        return Parse(contents.str());
    } catch (const std::exception& e) {
        Error(std::string("exception while loading: ") + e.what());
        return std::nullopt;
    } catch (...) {
        Error("unknown exception while loading");
        return std::nullopt;
    }
}

} // namespace Vixen::AppFlow
