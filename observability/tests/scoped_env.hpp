// El compilador de Microsoft (MSVC) suele mostrar una advertencia (C4996) 
// diciendo que getenv es “insegura”. Es solo un aviso, no un error, y el 
// código compila. Solo se vuelve problema si el CI del equipo trata las 
// advertencias como errores. Si pasa, se silencia en CMake con 
// target_compile_definitions(tecflix_telemetry PRIVATE _CRT_SECURE_NO_WARNINGS).

#pragma once

#include <cstdlib>
#include <optional>
#include <string>

#ifdef _WIN32
inline void SetEnv(const char* name, const char* value) { _putenv_s(name, value); }
inline void UnsetEnv(const char* name) { _putenv_s(name, ""); }  // en Windows, valor vacío = borrar
#else
inline void SetEnv(const char* name, const char* value) { setenv(name, value, 1); }
inline void UnsetEnv(const char* name) { unsetenv(name); }
#endif

// Fija una variable de entorno mientras vive el objeto y la restaura al salir (RAII).
// value == nullptr significa "dejarla sin definir".
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        if (const char* old = std::getenv(name)) old_ = old;
        if (value) SetEnv(name, value);
        else UnsetEnv(name);
    }
    ~ScopedEnv() {
        if (old_) SetEnv(name_, old_->c_str());
        else UnsetEnv(name_);
    }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    const char* name_;
    std::optional<std::string> old_;
};