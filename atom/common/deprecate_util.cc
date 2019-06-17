// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/common/deprecate_util.h"

#include "base/stl_util.h"
#include "native_mate/native_mate/converter.h"

namespace {

v8::Local<v8::Value> OptionalStringToV8(
    v8::Isolate* isolate,
    const base::Optional<std::string>& input) {
  if (input) {
    return mate::StringToV8(isolate, input->c_str());
  } else {
    return v8::Undefined(isolate);
  }
}

}  // namespace

namespace atom {

// msg/type/code arguments match Node's process.emitWarning() method. See
// https://nodejs.org/api/process.html#process_process_emitwarning_warning_type_code_ctor
bool EmitDeprecationWarning(node::Environment* env,
                            const std::string& warning_msg,
                            const base::Optional<std::string>& warning_type,
                            const base::Optional<std::string>& warning_code) {
  v8::HandleScope handle_scope(env->isolate());
  v8::Context::Scope context_scope(env->context());

  v8::Local<v8::Object> process = env->process_object();
  v8::Local<v8::Value> emit_warning;
  if (!process->Get(env->context(), env->emit_warning_string())
           .ToLocal(&emit_warning)) {
    return false;
  }

  if (!emit_warning->IsFunction())
    return false;

  v8::Local<v8::Value> args[] = {
      mate::ConvertToV8(env->isolate(), warning_msg),
      OptionalStringToV8(env->isolate(), warning_type),
      OptionalStringToV8(env->isolate(), warning_code),
  };

  if (emit_warning.As<v8::Function>()
          ->Call(env->context(), process, base::size(args), args)
          .IsEmpty()) {
    return false;
  }

  return true;
}

}  // namespace atom
