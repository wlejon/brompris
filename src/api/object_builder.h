#pragma once

#include "embed/embed.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace brompris::api {

namespace ev = bronze::embed;
using Value = bronze::Value;

/// Helper to build objects and namespaces property by property using bronze::embed.
/// Handles moving GC by rooting the target in an ev::Persistent.
struct ObjectBuilder {
    ev::Persistent obj;

    ObjectBuilder() : obj(ev::createObject()) {}
    explicit ObjectBuilder(Value existing) : obj(existing) {}

    void set(std::string_view name, Value v) {
        ev::Persistent valP(v);
        obj.set(ev::setProperty(obj.get(), name, valP.get()));
    }

    void set(std::string_view name, double d) {
        set(name, ev::fromDouble(d));
    }

    void set(std::string_view name, int64_t i) {
        set(name, ev::fromDouble(static_cast<double>(i)));
    }

    void set(std::string_view name, bool b) {
        set(name, ev::fromBool(b));
    }

    void set(std::string_view name, const std::string& s) {
        set(name, ev::fromUtf8(s));
    }

    void set(std::string_view name, const char* s) {
        set(name, ev::fromUtf8(s));
    }

    void def(std::string_view name, uint32_t arity, ev::NativeFn fn) {
        Value f = ev::makeFunction(std::move(fn), arity, name);
        obj.set(ev::setProperty(obj.get(), name, f));
    }

    Value get() const { return obj.get(); }
    Value build() const { return obj.get(); }
};

} // namespace brompris::api
