#pragma once

#include <httplib.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace tecflix::users {

// Error que solo existe en la capa HTTP: cuerpo mal formado, token ausente o
// falta de permisos. Los errores de negocio siguen siendo UserError.
class ApiError : public std::runtime_error {
 public:
  ApiError(httplib::StatusCode status, std::string code,
           const std::string& message)
      : std::runtime_error(message), status_(status), code_(std::move(code)) {}

  httplib::StatusCode Status() const { return status_; }
  const std::string& Code() const { return code_; }

 private:
  httplib::StatusCode status_;
  std::string code_;
};

// Único lugar que convierte errores en respuestas con el formato de SPEC 4.1:
// {"error":"snake_case_code","message":"texto"}. Los controladores solo lanzan
// excepciones, así ninguna ruta puede responder un error con otro formato.
class ApiErrors {
 public:
  static void Write(httplib::Response& response, httplib::StatusCode status,
                    const std::string& code, const std::string& message);

  // Traduce la excepción que escapó de un controlador. Lo que no sea un error de
  // la petición responde 500 sin detalles: el texto de un error interno puede
  // revelar rutas de archivos, y al cliente no le sirve.
  static void WriteException(httplib::Response& response,
                             const std::exception_ptr& error);

  // Da formato a los errores que httplib genera solo, como una ruta inexistente,
  // que llegan con el estado puesto y sin cuerpo.
  static void FillEmptyBody(httplib::Response& response);
};

}  // namespace tecflix::users
