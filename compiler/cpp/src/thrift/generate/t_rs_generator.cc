/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <iostream>
#include <string>

#include "thrift/generate/t_generator.h"
#include "thrift/platform.h"

using std::map;
using std::ofstream;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::vector;

static const string SERVICE_RESULT_VARIABLE("result_value");
static const string RESULT_STRUCT_SUFFIX("Result");
static const string RUST_RESERVED_WORDS[]
    = {"abstract", "alignof",  "as",     "become",  "box",   "break", "const", "continue", "crate",
       "do",       "else",     "enum",   "extern",  "false", "final", "fn",    "for",      "if",
       "impl",     "in",       "let",    "loop",    "macro", "match", "mod",   "move",     "mut",
       "offsetof", "override", "priv",   "proc",    "pub",   "pure",  "ref",   "return",   "Self",
       "self",     "sizeof",   "static", "struct",  "super", "trait", "true",  "type",     "typeof",
       "unsafe",   "unsized",  "use",    "virtual", "where", "while", "yield"};
const set<string> RUST_RESERVED_WORDS_SET(RUST_RESERVED_WORDS,
                                          RUST_RESERVED_WORDS
                                              + sizeof(RUST_RESERVED_WORDS)
                                                    / sizeof(RUST_RESERVED_WORDS[0]));

static const string SYNC_CLIENT_GENERIC_BOUND_VARS("<IP, OP>");
static const string SYNC_CLIENT_GENERIC_BOUNDS("where IP: TInputProtocol, OP: TOutputProtocol");

// FIXME: extract common TMessageIdentifier function
// FIXME: have to_rust_type deal with Option

class t_rs_generator : public t_generator {
public:
  t_rs_generator(t_program* program, const std::map<std::string, std::string>&, const std::string&)
    : t_generator(program) {
    gen_dir_ = get_out_dir();
  }

  /**
   * Init and close methods
   */

  void init_generator() override;
  void close_generator() override;
  std::string display_name() const override;

  /**
   * Program-level generation functions
   */

  void generate_typedef(t_typedef* ttypedef) override;
  void generate_enum(t_enum* tenum) override;
  void generate_const(t_const* tconst) override;
  void generate_struct(t_struct* tstruct) override;
  void generate_xception(t_struct* txception) override;
  void generate_service(t_service* tservice) override;

private:
  // struct type
  // T_REGULAR: user-defined struct in the IDL
  // T_ARGS: struct used to hold all service-call parameters
  // T_RESULT: struct used to hold all service-call returns and exceptions
  // T_EXCEPTION: user-defined exception in the IDL
  enum e_struct_type { T_REGULAR, T_ARGS, T_RESULT, T_EXCEPTION };

  // Directory to which generated code is written.
  string gen_dir_;

  // File to which generated code is written.
  ofstream_with_content_based_conditional_update f_gen_;

  // Write the common compiler attributes and module includes to the top of the auto-generated file.
  void render_attributes_and_includes();

  // Create the closure of Rust modules referenced by this service.
  void compute_service_referenced_modules(t_service* tservice,
                                          set<pair<string, string>>& referenced_modules);

  // Write the rust representation of an enum.
  void render_enum_definition(t_enum* tenum, const string& enum_name);

  // Write the impl blocks associated with the traits necessary to convert an enum to/from an i32.
  void render_enum_conversion(t_enum* tenum, const string& enum_name);

  // Write the impl block associated with the rust representation of an enum. This includes methods
  // to write the enum to a protocol, read it from a protocol, etc.
  void render_enum_impl(t_enum* tenum, const string& enum_name);

  // Write a simple rust const value (ie. `pub const FOO: foo...`).
  void render_const_value(const string& name, t_type* ttype, t_const_value* tvalue);

  // Write a constant list, set, map or struct. These constants require allocation and cannot be
  // defined using a 'pub const'. As a result, I create a holder struct with a single `const_value`
  // method that returns the initialized instance.
  void render_const_value_holder(const string& name, t_type* ttype, t_const_value* tvalue);

  // Write the actual const value - the right side of a const definition.
  void render_const_value(t_type* ttype,
                          t_const_value* tvalue,
                          bool is_owned = true,
                          bool is_inline = true);

  // Write a const struct (returned from `const_value` method).
  void render_const_struct(t_type* ttype, t_const_value* tvalue);

  // Write a const list (returned from `const_value` method).
  void render_const_list(t_type* ttype, t_const_value* tvalue);

  // Write a const set (returned from `const_value` method).
  void render_const_set(t_type* ttype, t_const_value* tvalue);

  // Write a const map (returned from `const_value` method).
  void render_const_map(t_type* ttype, t_const_value* tvalue);

  // Write the rust representation of a thrift struct to the generated file. Set `struct_type` to
  // `T_ARGS` if rendering the struct used to pack arguments for a service call. When `struct_type`
  // is `T_ARGS` the struct and its members have module visibility, and all fields are required.
  // When `struct_type` is anything else the struct and its members have public visibility and
  // fields have the visibility set in their definition.
  void render_struct(const string& struct_name,
                     t_struct* tstruct,
                     t_rs_generator::e_struct_type struct_type);

  // Write the comment block preceding a type definition (and implementation).
  void render_type_comment(const string& struct_name);

  // Write the rust representation of a thrift struct. Supports argument structs, result structs,
  // user-defined structs and exception structs. The exact struct type to be generated is controlled
  // by the `struct_type` parameter, which (among other things) modifies the visibility of the
  // written struct and members, controls which trait implementations are generated.
  void render_struct_definition(const string& struct_name,
                                t_struct* tstruct,
                                t_rs_generator::e_struct_type struct_type);

  // Writes the impl block associated with the rust representation of a struct. At minimum this
  // contains the methods to read from a protocol and write to a protocol. Additional methods may
  // be generated depending on `struct_type`.
  void render_struct_impl(const string& struct_name,
                          t_struct* tstruct,
                          t_rs_generator::e_struct_type struct_type);

  // Generate a `fn new(...)` for a struct with name `struct_name` and type `t_struct`. The
  // auto-generated code may include generic type parameters to make the constructor more ergonomic.
  // `struct_type` controls the visibility of the generated constructor.
  void render_struct_constructor(const string& struct_name,
                                 t_struct* tstruct,
                                 t_rs_generator::e_struct_type struct_type);

  // Write the `ok_or` method added to all Thrift service call result structs. You can use this
  // method to convert a struct into a `Result` and use it in a `try!` or combinator chain.
  void render_result_struct_to_result_method(t_struct* tstruct);

  // Write the implementations for the `Error` and `Debug` traits. These traits are necessary for a
  // user-defined exception to be properly handled as Rust errors.
  void render_exception_struct_error_trait_impls(const string& struct_name, t_struct* tstruct);

  // Write the function that serializes a struct to its wire representation. If `struct_type` is
  // `T_ARGS` then all fields are considered "required", if not, the default optionality is used.
  void render_struct_sync_write(t_struct* tstruct, t_rs_generator::e_struct_type struct_type);

  // Helper function that serializes a single struct field to its wire representation. Unpacks the
  // variable (since it may be optional) and serializes according to the optionality rules required
  // by `req`. Variables in auto-generated code are passed by reference. Since this function may be
  // called in contexts where the variable is *already* a reference you can set `field_var_is_ref`
  // to `true` to avoid generating an extra, unnecessary `&` that the compiler will have to
  // automatically dereference.
  void render_struct_field_sync_write(const string& field_var,
                                      bool field_var_is_ref,
                                      t_field* tfield,
                                      t_field::e_req req);

  // Write the rust function that serializes a single type (i.e. a i32 etc.) to its wire
  // representation. Variables in auto-generated code are passed by reference. Since this function
  // may be called in contexts where the variable is *already* a reference you can set
  // `type_var_is_ref` to `true` to avoid generating an extra, unnecessary `&` that the compiler
  // will have to automatically dereference.
  void render_type_sync_write(const string& type_var, bool type_var_is_ref, t_type* ttype);

  // Write a list to the output protocol. `list_variable` is the variable containing the list
  // that will be written to the output protocol.
  // Variables in auto-generated code are passed by reference. Since this function may be called in
  // contexts where the variable is *already* a reference you can set `list_var_is_ref` to `true` to
  // avoid generating an extra, unnecessary `&` that the compiler will have to automatically
  // dereference.
  void render_list_sync_write(const string& list_var, bool list_var_is_ref, t_list* tlist);

  // Write a set to the output protocol. `set_variable` is the variable containing the set that will
  // be written to the output protocol.
  // Variables in auto-generated code are passed by reference. Since this function may be called in
  // contexts where the variable is *already* a reference you can set `set_var_is_ref` to `true` to
  // avoid generating an extra, unnecessary `&` that the compiler will have to automatically
  // dereference.
  void render_set_sync_write(const string& set_var, bool set_var_is_ref, t_set* tset);

  // Write a map to the output protocol. `map_variable` is the variable containing the map that will
  // be written to the output protocol.
  // Variables in auto-generated code are passed by reference. Since this function may be called in
  // contexts where the variable is *already* a reference you can set `map_var_is_ref` to `true` to
  // avoid generating an extra, unnecessary `&` that the compiler will have to automatically
  // dereference.
  void render_map_sync_write(const string& map_var, bool map_var_is_ref, t_map* tset);

  // Return `true` if we need to dereference ths type when writing an element from a container.
  // Iterations on rust containers are performed as follows: `for v in &values { ... }`
  // where `v` has type `&RUST_TYPE` All defined functions take primitives by value, so, if the
  // rendered code is calling such a function it has to dereference `v`.
  bool needs_deref_on_container_write(t_type* ttype);

  // Return the variable (including all dereferences) required to write values from a rust container
  // to the output protocol. For example, if you were iterating through a container and using the
  // temp variable `v` to represent each element, then `ttype` is the type stored in the container
  // and `base_var` is "v". The return value is the actual string you will have to use to properly
  // reference the temp variable for writing to the output protocol.
  string string_container_write_variable(t_type* ttype, const string& base_var);

  // Write the code to read bytes from the wire into the given `t_struct`. `struct_name` is the
  // actual Rust name of the `t_struct`. If `struct_type` is `T_ARGS` then all struct fields are
  // necessary. Otherwise, the field's default optionality is used.
  void render_struct_sync_read(const string& struct_name,
                               t_struct* tstruct,
                               t_rs_generator::e_struct_type struct_type);

  // Write the rust function that deserializes a single type (i.e. i32 etc.) from its wire
  // representation. Set `is_boxed` to `true` if the resulting value should be wrapped in a
  // `Box::new(...)`.
  void render_type_sync_read(const string& type_var, t_type* ttype, bool is_boxed = false);

  // Read the wire representation of a list and convert it to its corresponding rust implementation.
  // The deserialized list is stored in `list_variable`.
  void render_list_sync_read(t_list* tlist, const string& list_variable);

  // Read the wire representation of a set and convert it to its corresponding rust implementation.
  // The deserialized set is stored in `set_variable`.
  void render_set_sync_read(t_set* tset, const string& set_variable);

  // Read the wire representation of a map and convert it to its corresponding rust implementation.
  // The deserialized map is stored in `map_variable`.
  void render_map_sync_read(t_map* tmap, const string& map_variable);

  // Return a temporary variable used to store values when deserializing nested containers.
  string struct_field_read_temp_variable(t_field* tfield);

  // Top-level function that calls the various render functions necessary to write the rust
  // representation of a thrift union (i.e. an enum).
  void render_union(t_struct* tstruct);

  // Write the enum corresponding to the Thrift union.
  void render_union_definition(const string& union_name, t_struct* tstruct);

  // Write the enum impl (with read/write functions) for the Thrift union.
  void render_union_impl(const string& union_name, t_struct* tstruct);

  // Write the `ENUM::write_to_out_protocol` function.
  void render_union_sync_write(const string& union_name, t_struct* tstruct);

  // Write the `ENUM::read_from_in_protocol` function.
  void render_union_sync_read(const string& union_name, t_struct* tstruct);

  // Top-level function that calls the various render functions necessary to write the rust
  // representation of a Thrift client.
  void render_sync_client(t_service* tservice);

  // Write the trait with the service-call methods for `tservice`.
  void render_sync_client_trait(t_service* tservice);

  // Write the trait to be implemented by the client impl if end users can use it to make service
  // calls.
  void render_sync_client_marker_trait(t_service* tservice);

  // Write the code to create the Thrift service sync client struct and its matching 'impl' block.
  void render_sync_client_definition_and_impl(const string& client_impl_name);

  // Write the code to create the `SyncClient::new` functions as well as any other functions
  // callers would like to use on the Thrift service sync client.
  void render_sync_client_lifecycle_functions(const string& client_struct);

  // Write the code to create the impl block for the `TThriftClient` trait. Since generated
  // Rust Thrift clients perform all their operations using methods defined in this trait, we
  // have to implement it for the client structs.
  void render_sync_client_tthriftclient_impl(const string& client_impl_name);

  // Write the marker traits for any service(s) being extended, including the one for the current
  // service itself (i.e. `tservice`)
  void render_sync_client_marker_trait_impls(t_service* tservice, const string& impl_struct_name);

  // Generate a list of all the traits this Thrift client struct extends.
  string sync_client_marker_traits_for_extension(t_service* tservice);

  // Top-level function that writes the code to make the Thrift service calls.
  void render_sync_client_process_impl(t_service* tservice);

  // Write the actual function that calls out to the remote service and processes its response.
  void render_sync_send_recv_wrapper(t_function* tfunc);

  // Write the `send` functionality for a Thrift service call represented by a
  // `t_service->t_function`.
  void render_sync_send(t_function* tfunc);

  // Write the `recv` functionality for a Thrift service call represented by a
  // `t_service->t_function`. This method is only rendered if the function is *not* oneway.
  void render_sync_recv(t_function* tfunc);

  void render_sync_processor(t_service* tservice);

  void render_sync_handler_trait(t_service* tservice);
  void render_sync_processor_definition_and_impl(t_service* tservice);
  void render_sync_process_delegation_functions(t_service* tservice);
  void render_sync_process_function(t_function* tfunc, const string& handler_type);
  void render_process_match_statements(t_service* tservice);
  void render_sync_handler_succeeded(t_function* tfunc);
  void render_sync_handler_failed(t_function* tfunc);
  void render_sync_handler_failed_user_exception_branch(t_function* tfunc);
  void render_sync_handler_failed_application_exception_branch(t_function* tfunc,
                                                               const string& app_err_var);
  void render_sync_handler_failed_default_exception_branch(t_function* tfunc);
  void render_sync_handler_send_exception_response(t_function* tfunc, const string& err_var);
  void render_service_call_structs(t_service* tservice);
  void render_service_call_args_struct(t_function* tfunc);
  void render_service_call_result_value_struct(t_function* tfunc);

  string handler_successful_return_struct(t_function* tfunc);

  // Writes the result of `render_thrift_error_struct` wrapped in an `Err(thrift::Error(...))`.
  void render_thrift_error(const string& error_kind,
                           const string& error_struct,
                           const string& sub_error_kind,
                           const string& error_message);

  // Write a thrift::Error variant struct. Error structs take the form:
  // ```
  // pub struct error_struct {
  //   kind: sub_error_kind,
  //   message: error_message,
  // }
  // ```
  // A concrete example is:
  // ```
  //  pub struct ApplicationError {
  //    kind: ApplicationErrorKind::Unknown,
  //    message: "This is some error message",
  //  }
  // ```
  void render_thrift_error_struct(const string& error_struct,
                                  const string& sub_error_kind,
                                  const string& error_message);

  // Return a string containing all the unpacked service call args given a service call function
  // `t_function`. Prepends the args with either `&mut self` or `&self` and includes the arg types
  // in the returned string, for example:
  // `fn foo(&mut self, field_0: String)`.
  string rust_sync_service_call_declaration(t_function* tfunc, bool self_is_mutable);

  // Return a string containing all the unpacked service call args given a service call function
  // `t_function`. Only includes the arg names, each of which is prefixed with the optional prefix
  // `field_prefix`, for example: `self.field_0`.
  string rust_sync_service_call_invocation(t_function* tfunc, const string& field_prefix = "");

  // Return a string containing all fields in the struct `tstruct` for use in a function
  // declaration. Each field is followed by its type, for example: `field_0: String`.
  string struct_to_declaration(t_struct* tstruct, t_rs_generator::e_struct_type struct_type);

  // Return a string containing all fields in the struct `tstruct` for use in a function call,
  // for example: `field_0: String`.
  string struct_to_invocation(t_struct* tstruct, const string& field_prefix = "");

  // Write the documentation for a struct, service-call or other documentation-annotated element.
  void render_rustdoc(t_doc* tdoc);

  // Return `true` if the true type of `ttype` is a thrift double, `false` otherwise.
  bool is_double(t_type* ttype);

  // Return a string representing the rust type given a `t_type`.
  string to_rust_type(t_type* ttype);

  // Return a string representing the `const` rust type given a `t_type`
  string to_rust_const_type(t_type* ttype);

  // Return a string representing the rift `protocol::TType` given a `t_type`.
  string to_rust_field_type_enum(t_type* ttype);

  // Return the default value to be used when initializing a struct field which has `OPT_IN_REQ_OUT`
  // optionality.
  string opt_in_req_out_value(t_type* ttype);

  // Return `true` if we can write a const of the form `pub const FOO: ...`.
  bool can_generate_simple_const(t_type* ttype);

  // Return `true` if we cannot write a standard Rust constant (because the type needs some
  // allocation).
  bool can_generate_const_holder(t_type* ttype);

  // Return `true` if this type is a void, and should be represented by the rust `()` type.
  bool is_void(t_type* ttype);

  t_field::e_req actual_field_req(t_field* tfield, t_rs_generator::e_struct_type struct_type);

  // Return `true` if this `t_field::e_req` is either `t_field::T_OPTIONAL` or
  // `t_field::T_OPT_IN_REQ_OUT` and needs to be wrapped by an `Option<TYPE_NAME>`, `false`
  // otherwise.
  bool is_optional(t_field::e_req req);

  // Return `true` if the service call has arguments, `false` otherwise.
  bool has_args(t_function* tfunc);

  // Return `true` if a service call has non-`()` arguments, `false` otherwise.
  bool has_non_void_args(t_function* tfunc);

  // Return `pub ` (notice trailing whitespace!) if the struct should be public, `` (empty string)
  // otherwise.
  string visibility_qualifier(t_rs_generator::e_struct_type struct_type);

  // Returns the namespace prefix for a given Thrift service. If the type is defined in the
  // presently-computed Thrift program, then an empty string is returned.
  string rust_namespace(t_service* tservice);

  // Returns the namespace prefix for a given Thrift type. If the type is defined in the
  // presently-computed Thrift program, then an empty string is returned.
  string rust_namespace(t_type* ttype);

  // Returns the camel-cased name for a Rust struct type. Handles the case where
  // `tstruct->get_name()` is a reserved word.
  string rust_struct_name(t_struct* tstruct);

  // Returns the snake-cased name for a Rust field or local variable. Handles the case where
  // `tfield->get_name()` is a reserved word.
  string rust_field_name(t_field* tstruct);

  // Returns the camel-cased name for a Rust union type. Handles the case where
  // `tstruct->get_name()` is a reserved word.
  string rust_union_field_name(t_field* tstruct);

  // Converts any variable name into a 'safe' variant that does not clash with any Rust reserved
  // keywords.
  string rust_safe_name(const string& name);

  // Return `true` if the name is a reserved Rust keyword, `false` otherwise.
  bool is_reserved(const string& name);

  // Return the name of the function that users will invoke to make outgoing service calls.
  string service_call_client_function_name(t_function* tfunc);

  // Return the name of the function that users will have to implement to handle incoming service
  // calls.
  string service_call_handler_function_name(t_function* tfunc);

  // Return the name of the struct used to pack the arguments for the thrift service call.
  string service_call_args_struct_name(t_function* tfunc);

  // Return the name of the struct used to pack the return value
  // and user-defined exceptions for the thrift service call.
  string service_call_result_struct_name(t_function* tfunc);

  string rust_sync_client_marker_trait_name(t_service* tservice);

  // Return the trait name for the sync service client given a `t_service`.
  string rust_sync_client_trait_name(t_service* tservice);

  // Return the name for the sync service client struct given a `t_service`.
  string rust_sync_client_impl_name(t_service* tservice);

  // Return the trait name that users will have to implement for the server half of a Thrift
  // service.
  string rust_sync_handler_trait_name(t_service* tservice);

  // Return the struct name for the  server half of a Thrift service.
  string rust_sync_processor_name(t_service* tservice);

  // Return the struct name for the struct that contains all the service-call implementations for
  // the server half of a Thrift service.
  string rust_sync_processor_impl_name(t_service* tservice);

  // Return the variant name for an enum variant
  string rust_enum_variant_name(const string& name);

  // Properly uppercase names for use in Rust.
  string rust_upper_case(const string& name);

  // Snake-case field, parameter and function names and make them Rust friendly.
  string rust_snake_case(const string& name);

  // Camel-case type/variant names and make them Rust friendly.
  string rust_camel_case(const string& name);

  // Replace all instances of `search_string` with `replace_string` in `target`.
  void string_replace(string& target, const string& search_string, const string& replace_string);

  // Adjust field identifier to correctly handle unspecified field identifiers
  // THRIFT-4953
  string rust_safe_field_id(int32_t id);
};

void t_rs_generator::init_generator() {
  // make output directory for this thrift program
  MKDIR(gen_dir_.c_str());

  // create the file into which we're going to write the generated code
  string f_gen_name = gen_dir_ + "/" + rust_snake_case(get_program()->get_name()) + ".rs";
  f_gen_.open(f_gen_name.c_str());

  // header comment
  f_gen_ << "// " << autogen_summary() << '\n';
  f_gen_ << "// DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING" << '\n';
  f_gen_ << '\n';

  render_attributes_and_includes();
}

void t_rs_generator::render_attributes_and_includes() {
  // turn off some compiler/clippy warnings

  // code may not be used
  f_gen_ << "#![allow(dead_code)]" << '\n';
  // code always includes BTreeMap/BTreeSet/OrderedFloat
  f_gen_ << "#![allow(unused_imports)]" << '\n';
  // code might not include imports from crates
  f_gen_ << "#![allow(unused_extern_crates)]" << '\n';
  // constructors take *all* struct parameters, which can trigger the "too many arguments" warning
  // some auto-gen'd types can be deeply nested. clippy recommends factoring them out which is hard
  // to autogen some methods may start with "is_"
  // FIXME: re-enable the 'vec_box' lint see:
  // [THRIFT-5364](https://issues.apache.org/jira/browse/THRIFT-5364) This can happen because we
  // automatically generate a Vec<Box<Type>> when the type is a typedef and it's a forward typedef.
  // This (typedef + forward typedef) can happen in two situations:
  // 1. When the type is recursive
  // 2. When you define types out of order
  f_gen_ << "#![allow(clippy::too_many_arguments, clippy::type_complexity, clippy::vec_box, "
            "clippy::wrong_self_convention)]"
         << '\n';
  // prevent rustfmt from running against this file
  // lines are too long, code is (thankfully!) not visual-indented, etc.
  // can't use #[rustfmt::skip] see: https://github.com/rust-lang/rust/issues/54726
  f_gen_ << "#![cfg_attr(rustfmt, rustfmt_skip)]" << '\n';
  f_gen_ << '\n';

  // add standard includes
  f_gen_ << "use std::cell::RefCell;" << '\n';
  f_gen_ << "use std::collections::{BTreeMap, BTreeSet};" << '\n';
  f_gen_ << "use std::convert::{From, TryFrom};" << '\n';
  f_gen_ << "use std::default::Default;" << '\n';
  f_gen_ << "use std::error::Error;" << '\n';
  f_gen_ << "use std::fmt;" << '\n';
  f_gen_ << "use std::fmt::{Display, Formatter};" << '\n';
  f_gen_ << "use std::rc::Rc;" << '\n';
  f_gen_ << '\n';
  f_gen_ << "use thrift::OrderedFloat;" << '\n';
  f_gen_ << "use thrift::{ApplicationError, ApplicationErrorKind, ProtocolError, "
            "ProtocolErrorKind, TThriftClient};"
         << '\n';
  f_gen_ << "use thrift::protocol::{TFieldIdentifier, TListIdentifier, TMapIdentifier, "
            "TMessageIdentifier, TMessageType, TInputProtocol, TOutputProtocol, TSerializable, "
            "TSetIdentifier, TStructIdentifier, TType};"
         << '\n';
  f_gen_ << "use thrift::protocol::field_id;" << '\n';
  f_gen_ << "use thrift::protocol::verify_expected_message_type;" << '\n';
  f_gen_ << "use thrift::protocol::verify_expected_sequence_number;" << '\n';
  f_gen_ << "use thrift::protocol::verify_expected_service_call;" << '\n';
  f_gen_ << "use thrift::protocol::verify_required_field_exists;" << '\n';
  f_gen_ << "use thrift::server::TProcessor;" << '\n';
  f_gen_ << '\n';

  // add all the program includes
  // NOTE: this is more involved than you would expect because of service extension
  // Basically, I have to find the closure of all the services and include their modules at the
  // top-level

  set<pair<string, string>> referenced_modules; // set<module, namespace>

  // first, start by adding explicit thrift includes
  const vector<t_program*> includes = get_program()->get_includes();
  vector<t_program*>::const_iterator includes_iter;
  for (includes_iter = includes.begin(); includes_iter != includes.end(); ++includes_iter) {
    referenced_modules.insert(
        std::make_pair((*includes_iter)->get_name(), (*includes_iter)->get_namespace("rs")));
  }

  // next, recursively iterate through all the services and add the names of any programs they
  // reference
  const vector<t_service*> services = get_program()->get_services();
  vector<t_service*>::const_iterator service_iter;
  for (service_iter = services.begin(); service_iter != services.end(); ++service_iter) {
    compute_service_referenced_modules(*service_iter, referenced_modules);
  }

  // finally, write all the "pub use..." declarations
  if (!referenced_modules.empty()) {
    set<pair<string, string>>::iterator module_iter;
    for (module_iter = referenced_modules.begin(); module_iter != referenced_modules.end();
         ++module_iter) {
      string module_name((*module_iter).first);

      string module_namespace((*module_iter).second);
      string_replace(module_namespace, ".", "::");

      if (module_namespace.empty()) {
        f_gen_ << "use crate::" << rust_snake_case(module_name) << ";" << '\n';
      } else {
        f_gen_ << "use crate::" << module_namespace << "::" << rust_snake_case(module_name) << ";"
               << '\n';
      }
    }
    f_gen_ << '\n';
  }
}

void t_rs_generator::compute_service_referenced_modules(
    t_service* tservice,
    set<pair<string, string>>& referenced_modules) {
  t_service* extends = tservice->get_extends();
  if (extends) {
    if (extends->get_program() != get_program()) {
      referenced_modules.insert(std::make_pair(extends->get_program()->get_name(),
                                               extends->get_program()->get_namespace("rs")));
    }
    compute_service_referenced_modules(extends, referenced_modules);
  }
}

void t_rs_generator::close_generator() {
  f_gen_.close();
}

//-----------------------------------------------------------------------------
//
// Consts
//
// NOTE: consider using macros to generate constants
//
//-----------------------------------------------------------------------------

// This is worse than it should be because constants
// aren't (sensibly) limited to scalar types
void t_rs_generator::generate_const(t_const* tconst) {
  string name = tconst->get_name();
  t_type* ttype = tconst->get_type();
  t_const_value* tvalue = tconst->get_value();

  if (can_generate_simple_const(ttype)) {
    render_const_value(name, ttype, tvalue);
  } else if (can_generate_const_holder(ttype)) {
    render_const_value_holder(name, ttype, tvalue);
  } else {
    throw "cannot generate const for " + name;
  }
}

void t_rs_generator::render_const_value(const string& name, t_type* ttype, t_const_value* tvalue) {
  if (!can_generate_simple_const(ttype)) {
    throw "cannot generate simple rust constant for " + ttype->get_name();
  }

  f_gen_ << "pub const " << rust_upper_case(name) << ": " << to_rust_const_type(ttype) << " = ";
  render_const_value(ttype, tvalue, false);
  f_gen_ << ";" << '\n';
  f_gen_ << '\n';
}

void t_rs_generator::render_const_value_holder(const string& name,
                                               t_type* ttype,
                                               t_const_value* tvalue) {
  if (!can_generate_const_holder(ttype)) {
    throw "cannot generate constant holder for " + ttype->get_name();
  }

  string holder_name("Const" + rust_camel_case(name));

  f_gen_ << indent() << "pub struct " << holder_name << ";" << '\n';
  f_gen_ << indent() << "impl " << holder_name << " {" << '\n';
  indent_up();

  f_gen_ << indent() << "pub fn const_value() -> " << to_rust_type(ttype) << " {" << '\n';
  indent_up();
  render_const_value(ttype, tvalue, true, false);
  indent_down();
  f_gen_ << indent() << "}" << '\n';

  indent_down();
  f_gen_ << indent() << "}" << '\n';
  f_gen_ << '\n';
}

void t_rs_generator::render_const_value(t_type* ttype,
                                        t_const_value* tvalue,
                                        bool is_owned,
                                        bool is_inline) {
  if (!is_inline) {
    f_gen_ << indent();
  }

  if (ttype->is_base_type()) {
    t_base_type* tbase_type = (t_base_type*)ttype;
    switch (tbase_type->get_base()) {
    case t_base_type::TYPE_STRING:
      if (tbase_type->is_binary()) {
        if (is_owned) {
          f_gen_ << "\"" << tvalue->get_string() << "\""
                 << ".to_owned().into_bytes()";
        } else {
          f_gen_ << "b\"" << tvalue->get_string() << "\"";
        }
      } else {
        f_gen_ << "\"" << tvalue->get_string() << "\"";
        if (is_owned) {
          f_gen_ << ".to_owned()";
        }
      }
      break;
    case t_base_type::TYPE_UUID:
      f_gen_ << "Uuid::parse_str(\"" << tvalue->get_string() << "\").unwrap()";
      break;
    case t_base_type::TYPE_BOOL:
      f_gen_ << (tvalue->get_integer() ? "true" : "false");
      break;
    case t_base_type::TYPE_I8:
    case t_base_type::TYPE_I16:
    case t_base_type::TYPE_I32:
    case t_base_type::TYPE_I64:
      f_gen_ << tvalue->get_integer();
      break;
    case t_base_type::TYPE_DOUBLE:
      f_gen_ << "OrderedFloat::from(" << tvalue->get_double() << "_f64)";
      break;
    default:
      throw "cannot generate const value for " + t_base_type::t_base_name(tbase_type->get_base());
    }
  } else if (ttype->is_typedef()) {
    render_const_value(get_true_type(ttype), tvalue, is_owned, true);
  } else if (ttype->is_enum()) {
    f_gen_ << "{" << '\n';
    indent_up();
    f_gen_ << indent() << to_rust_type(ttype) << "::from(" << tvalue->get_integer()
           << ")" << '\n';
    indent_down();
    f_gen_ << indent() << "}";
  } else if (ttype->is_struct() || ttype->is_xception()) {
    render_const_struct(ttype, tvalue);
  } else if (ttype->is_container()) {
    // all of them use vec! or from(), extra block is no longer needed
    if (ttype->is_list()) {
      render_const_list(ttype, tvalue);
    } else if (ttype->is_set()) {
      render_const_set(ttype, tvalue);
    } else if (ttype->is_map()) {
      render_const_map(ttype, tvalue);
    } else {
      throw "cannot generate const container value for " + ttype->get_name();
    }
  } else {
    throw "cannot generate const value for " + ttype->get_name();
  }

  if (!is_inline) {
    f_gen_ << '\n';
  }
}

void t_rs_generator::render_const_struct(t_type* ttype, t_const_value*) {
  if (((t_struct*)ttype)->is_union()) {
    f_gen_ << "{" << '\n';
    indent_up();
    f_gen_ << indent() << "unimplemented!()" << '\n';
    indent_down();
    f_gen_ << indent() << "}";
  } else {
    f_gen_ << "{" << '\n';
    indent_up();
    f_gen_ << indent() << "unimplemented!()" << '\n';
    indent_down();
    f_gen_ << indent() << "}";
  }
}

void t_rs_generator::render_const_list(t_type* ttype, t_const_value* tvalue) {
  t_type* elem_type = ((t_list*)ttype)->get_elem_type();
  f_gen_ << "vec![" << '\n';
  indent_up();
  const vector<t_const_value*>& elems = tvalue->get_list();
  vector<t_const_value*>::const_iterator elem_iter;
  for (elem_iter = elems.begin(); elem_iter != elems.end(); ++elem_iter) {
    f_gen_ << indent();
    t_const_value* elem_value = (*elem_iter);
    render_const_value(elem_type, elem_value);
    f_gen_ << "," << '\n';
  }
  indent_down();
  f_gen_ << indent() << "]";
}

void t_rs_generator::render_const_set(t_type* ttype, t_const_value* tvalue) {
  t_type* elem_type = ((t_set*)ttype)->get_elem_type();
  f_gen_ << "BTreeSet::from([" << '\n';
  indent_up();
  const vector<t_const_value*>& elems = tvalue->get_list();
  vector<t_const_value*>::const_iterator elem_iter;
  for (elem_iter = elems.begin(); elem_iter != elems.end(); ++elem_iter) {
    f_gen_ << indent();
    t_const_value* elem_value = (*elem_iter);
    render_const_value(elem_type, elem_value);
    f_gen_ << "," << '\n';
  }
  indent_down();
  f_gen_ << indent() << "])";
}

void t_rs_generator::render_const_map(t_type* ttype, t_const_value* tvalue) {
  t_type* key_type = ((t_map*)ttype)->get_key_type();
  t_type* val_type = ((t_map*)ttype)->get_val_type();
  f_gen_ << "BTreeMap::from([" << '\n';
  indent_up();
  const map<t_const_value*, t_const_value*, t_const_value::value_compare>& elems
      = tvalue->get_map();
  map<t_const_value*, t_const_value*, t_const_value::value_compare>::const_iterator elem_iter;
  for (elem_iter = elems.begin(); elem_iter != elems.end(); ++elem_iter) {
    t_const_value* key_value = elem_iter->first;
    t_const_value* val_value = elem_iter->second;

    f_gen_ << indent() << "(" << '\n';
    indent_up();
    f_gen_ << indent();
    render_const_value(key_type, key_value);
    f_gen_ << "," << '\n';
    f_gen_ << indent();
    render_const_value(val_type, val_value);
    f_gen_ << "," << '\n';
    indent_down();
    f_gen_ << indent() << ")," << '\n';
  }
  indent_down();
  f_gen_ << indent() << "])";
}

//-----------------------------------------------------------------------------
//
// Typedefs
//
//-----------------------------------------------------------------------------

void t_rs_generator::generate_typedef(t_typedef* ttypedef) {
  std::string actual_type = to_rust_type(ttypedef->get_type());
  f_gen_ << "pub type " << rust_safe_name(ttypedef->get_symbolic()) << " = " << actual_type << ";"
         << '\n';
  f_gen_ << '\n';
}

//-----------------------------------------------------------------------------
//
// Enums
//
//-----------------------------------------------------------------------------

void t_rs_generator::generate_enum(t_enum* tenum) {
  string enum_name(rust_camel_case(tenum->get_name()));
  render_enum_definition(tenum, enum_name);
  render_enum_impl(tenum, enum_name);
  render_enum_conversion(tenum, enum_name);
}

void t_rs_generator::render_enum_definition(t_enum* tenum, const string& enum_name) {
  render_rustdoc((t_doc*)tenum);
  f_gen_ << "#[derive(Copy, Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]" << '\n';
  f_gen_ << "pub struct " << enum_name << "(pub i32);" << '\n';
  f_gen_ << '\n';
}

void t_rs_generator::render_enum_impl(t_enum* tenum, const string& enum_name) {
  f_gen_ << "impl " << enum_name << " {" << '\n';
  indent_up();

  vector<t_enum_value*> constants = tenum->get_constants();

  // associated constants for each IDL-defined enum variant
  {
    vector<t_enum_value*>::iterator constants_iter;
    for (constants_iter = constants.begin(); constants_iter != constants.end(); ++constants_iter) {
      t_enum_value* val = (*constants_iter);
      render_rustdoc((t_doc*)val);
      f_gen_ << indent() << "pub const " << rust_enum_variant_name(val->get_name()) << ": "
             << enum_name << " = " << enum_name << "(" << val->get_value() << ")"
             << ";" << '\n';
    }
  }

  // array containing all IDL-defined enum variants
  {
    f_gen_ << indent() << "pub const ENUM_VALUES: &'static [Self] = &[" << '\n';
    indent_up();
    vector<t_enum_value*>::iterator constants_iter;
    for (constants_iter = constants.begin(); constants_iter != constants.end(); ++constants_iter) {
      t_enum_value* val = (*constants_iter);
      f_gen_ << indent() << "Self::" << rust_enum_variant_name(val->get_name()) << "," << '\n';
    }
    indent_down();
    f_gen_ << indent() << "];" << '\n';
  }

  indent_down();
  f_gen_ << "}" << '\n';
  f_gen_ << '\n';

  f_gen_ << "impl TSerializable for " << enum_name << " {" << '\n';
  indent_up();

  f_gen_ << indent() << "#[allow(clippy::trivially_copy_pass_by_ref)]" << '\n';
  f_gen_
      << indent()
      << "fn write_to_out_protocol(&self, o_prot: &mut dyn TOutputProtocol) -> thrift::Result<()> {"
      << '\n';
  indent_up();
  f_gen_ << indent() << "o_prot.write_i32(self.0)" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';

  f_gen_ << indent()
         << "fn read_from_in_protocol(i_prot: &mut dyn TInputProtocol) -> thrift::Result<"
         << enum_name << "> {" << '\n';
  indent_up();
  f_gen_ << indent() << "let enum_value = i_prot.read_i32()?;" << '\n';
  f_gen_ << indent() << "Ok(" << enum_name << "::from(enum_value)"
         << ")" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';

  indent_down();
  f_gen_ << "}" << '\n';
  f_gen_ << '\n';
}

void t_rs_generator::render_enum_conversion(t_enum* tenum, const string& enum_name) {
  // From trait: i32 -> ENUM_TYPE
  f_gen_ << "impl From<i32> for " << enum_name << " {" << '\n';
  indent_up();
  f_gen_ << indent() << "fn from(i: i32) -> Self {" << '\n';
  indent_up();
  f_gen_ << indent() << "match i {" << '\n';
  indent_up();
  vector<t_enum_value*> constants = tenum->get_constants();
  vector<t_enum_value*>::iterator constants_iter;
  for (constants_iter = constants.begin(); constants_iter != constants.end(); ++constants_iter) {
    t_enum_value* val = (*constants_iter);
    f_gen_ << indent() << val->get_value() << " => " << enum_name
           << "::" << rust_enum_variant_name(val->get_name()) << "," << '\n';
  }
  f_gen_ << indent() << "_ => " << enum_name << "(i)" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  indent_down();
  f_gen_ << "}" << '\n';
  f_gen_ << '\n';

  // From trait: &i32 -> ENUM_TYPE
  f_gen_ << "impl From<&i32> for " << enum_name << " {" << '\n';
  indent_up();
  f_gen_ << indent() << "fn from(i: &i32) -> Self {" << '\n';
  indent_up();
  f_gen_ << indent() << enum_name << "::from(*i)" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  indent_down();
  f_gen_ << "}" << '\n';
  f_gen_ << '\n';

  // From trait: ENUM_TYPE -> int
  f_gen_ << "impl From<" << enum_name << "> for i32 {" << '\n';
  indent_up();
  f_gen_ << indent() << "fn from(e: " << enum_name << ") -> i32 {" << '\n';
  indent_up();
  f_gen_ << indent() << "e.0" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  indent_down();
  f_gen_ << "}" << '\n';
  f_gen_ << '\n';

  // From trait: &ENUM_TYPE -> int
  f_gen_ << "impl From<&" << enum_name << "> for i32 {" << '\n';
  indent_up();
  f_gen_ << indent() << "fn from(e: &" << enum_name << ") -> i32 {" << '\n';
  indent_up();
  f_gen_ << indent() << "e.0" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  indent_down();
  f_gen_ << "}" << '\n';
  f_gen_ << '\n';
}

//-----------------------------------------------------------------------------
//
// Structs, Unions and Exceptions
//
//-----------------------------------------------------------------------------

void t_rs_generator::generate_xception(t_struct* txception) {
  render_struct(rust_struct_name(txception), txception, t_rs_generator::T_EXCEPTION);
}

void t_rs_generator::generate_struct(t_struct* tstruct) {
  if (tstruct->is_union()) {
    render_union(tstruct);
  } else if (tstruct->is_struct()) {
    render_struct(rust_struct_name(tstruct), tstruct, t_rs_generator::T_REGULAR);
  } else {
    throw "cannot generate struct for exception";
  }
}

void t_rs_generator::render_struct(const string& struct_name,
                                   t_struct* tstruct,
                                   t_rs_generator::e_struct_type struct_type) {
  render_type_comment(struct_name);
  render_struct_definition(struct_name, tstruct, struct_type);
  render_struct_impl(struct_name, tstruct, struct_type);
  if (struct_type == t_rs_generator::T_EXCEPTION) {
    render_exception_struct_error_trait_impls(struct_name, tstruct);
  }
}

void t_rs_generator::render_struct_definition(const string& struct_name,
                                              t_struct* tstruct,
                                              t_rs_generator::e_struct_type struct_type) {
  render_rustdoc((t_doc*)tstruct);

  const vector<t_field*> members = tstruct->get_sorted_members();
  vector<t_field*>::const_iterator members_iter;
  bool need_default
      = struct_type == t_rs_generator::T_REGULAR || struct_type == t_rs_generator::T_EXCEPTION;
  for (members_iter = members.begin(); need_default && members_iter != members.end();
       ++members_iter) {
    t_field* member = *members_iter;
    if (!is_optional(member->get_req())) {
      need_default = false;
    }
  }
  f_gen_ << "#[derive(Clone, Debug" << (need_default ? ", Default" : "")
         << ", Eq, Hash, Ord, PartialEq, PartialOrd)]" << '\n';
  f_gen_ << visibility_qualifier(struct_type) << "struct " << struct_name << " {" << '\n';

  // render the members
  if (!members.empty()) {
    indent_up();

    for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
      t_field* member = (*members_iter);
      t_field::e_req member_req = actual_field_req(member, struct_type);

      string rust_type = to_rust_type(member->get_type());
      rust_type = is_optional(member_req) ? "Option<" + rust_type + ">" : rust_type;

      render_rustdoc((t_doc*)member);
      f_gen_ << indent() << visibility_qualifier(struct_type) << rust_field_name(member) << ": "
             << rust_type << "," << '\n';
    }

    indent_down();
  }

  f_gen_ << "}" << '\n';
  f_gen_ << '\n';
}

void t_rs_generator::render_exception_struct_error_trait_impls(const string& struct_name,
                                                               t_struct* tstruct) {
  // error::Error trait
  f_gen_ << "impl Error for " << struct_name << " {}" << '\n';
  f_gen_ << '\n';

  // convert::From trait
  f_gen_ << "impl From<" << struct_name << "> for thrift::Error {" << '\n';
  indent_up();
  f_gen_ << indent() << "fn from(e: " << struct_name << ") -> Self {" << '\n';
  indent_up();
  f_gen_ << indent() << "thrift::Error::User(Box::new(e))" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  indent_down();
  f_gen_ << "}" << '\n';
  f_gen_ << '\n';

  // fmt::Display trait
  f_gen_ << "impl Display for " << struct_name << " {" << '\n';
  indent_up();
  f_gen_ << indent() << "fn fmt(&self, f: &mut Formatter) -> fmt::Result {" << '\n';
  indent_up();
  f_gen_ << indent() << "write!(f, "
         << "\"remote service threw " << tstruct->get_name() << "\"" // use *original* name
         << ")" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  indent_down();
  f_gen_ << "}" << '\n';
  f_gen_ << '\n';
}

void t_rs_generator::render_struct_impl(const string& struct_name,
                                        t_struct* tstruct,
                                        t_rs_generator::e_struct_type struct_type) {
  f_gen_ << "impl " << struct_name << " {" << '\n';
  indent_up();

  if (struct_type == t_rs_generator::T_REGULAR || struct_type == t_rs_generator::T_EXCEPTION) {
    render_struct_constructor(struct_name, tstruct, struct_type);
  }

  if (struct_type == t_rs_generator::T_RESULT) {
    render_result_struct_to_result_method(tstruct);
  }

  if (struct_type == t_rs_generator::T_REGULAR || struct_type == t_rs_generator::T_EXCEPTION) {
    indent_down();
    f_gen_ << "}" << '\n';
    f_gen_ << '\n';

    f_gen_ << "impl TSerializable for " << struct_name << " {" << '\n';
    indent_up();
  }

  render_struct_sync_read(struct_name, tstruct, struct_type);
  render_struct_sync_write(tstruct, struct_type);

  indent_down();
  f_gen_ << "}" << '\n';
  f_gen_ << '\n';
}

void t_rs_generator::render_struct_constructor(const string& struct_name,
                                               t_struct* tstruct,
                                               t_rs_generator::e_struct_type struct_type) {
  const vector<t_field*>& members = tstruct->get_sorted_members();
  vector<t_field*>::const_iterator members_iter;

  // build the convenience type parameters that allows us to pass unwrapped values to a constructor
  // and have them automatically converted into Option<value>
  bool first_arg = true;

  ostringstream generic_type_parameters;
  ostringstream generic_type_qualifiers;
  for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
    t_field* member = (*members_iter);
    t_field::e_req member_req = actual_field_req(member, struct_type);

    if (is_optional(member_req)) {
      if (first_arg) {
        first_arg = false;
      } else {
        generic_type_parameters << ", ";
        generic_type_qualifiers << ", ";
      }
      generic_type_parameters << "F" << rust_safe_field_id(member->get_key());
      generic_type_qualifiers << "F" << rust_safe_field_id(member->get_key()) << ": Into<Option<"
                              << to_rust_type(member->get_type()) << ">>";
    }
  }

  string type_parameter_string = generic_type_parameters.str();
  if (type_parameter_string.length() != 0) {
    type_parameter_string = "<" + type_parameter_string + ">";
  }

  string type_qualifier_string = generic_type_qualifiers.str();
  if (type_qualifier_string.length() != 0) {
    type_qualifier_string = "where " + type_qualifier_string + " ";
  }

  // now build the actual constructor arg list
  // when we're building this list we have to use the type parameters in place of the actual type
  // names if necessary
  ostringstream args;
  first_arg = true;
  for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
    t_field* member = (*members_iter);
    t_field::e_req member_req = actual_field_req(member, struct_type);
    string member_name(rust_field_name(member));

    if (first_arg) {
      first_arg = false;
    } else {
      args << ", ";
    }

    if (is_optional(member_req)) {
      args << member_name << ": "
           << "F" << rust_safe_field_id(member->get_key());
    } else {
      args << member_name << ": " << to_rust_type(member->get_type());
    }
  }

  string arg_string = args.str();

  string visibility(visibility_qualifier(struct_type));
  f_gen_ << indent() << visibility << "fn new" << type_parameter_string << "(" << arg_string
         << ") -> " << struct_name << " " << type_qualifier_string << "{" << '\n';
  indent_up();

  if (members.empty()) {
    f_gen_ << indent() << struct_name << " {}" << '\n';
  } else {
    f_gen_ << indent() << struct_name << " {" << '\n';
    indent_up();

    for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
      t_field* member = (*members_iter);
      t_field::e_req member_req = actual_field_req(member, struct_type);
      string member_name(rust_field_name(member));

      if (is_optional(member_req)) {
        f_gen_ << indent() << member_name << ": " << member_name << ".into()," << '\n';
      } else {
        f_gen_ << indent() << member_name << "," << '\n';
      }
    }

    indent_down();
    f_gen_ << indent() << "}" << '\n';
  }

  indent_down();
  f_gen_ << indent() << "}" << '\n';
}

void t_rs_generator::render_result_struct_to_result_method(t_struct* tstruct) {
  // we don't use the rust struct name in this method, just the service call name
  string service_call_name = tstruct->get_name();

  // check that we actually have a result
  size_t index = service_call_name.find(RESULT_STRUCT_SUFFIX, 0);
  if (index == std::string::npos) {
    throw "result struct " + service_call_name + " missing result suffix";
  } else {
    service_call_name.replace(index, 6, "");
  }

  const vector<t_field*>& members = tstruct->get_sorted_members();
  vector<t_field*>::const_iterator members_iter;

  // find out what the call's expected return type was
  string rust_return_type = "()";
  for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
    t_field* member = (*members_iter);
    if (member->get_name() == SERVICE_RESULT_VARIABLE) { // don't have to check safe name here
      rust_return_type = to_rust_type(member->get_type());
      break;
    }
  }

  // NOTE: ideally I would generate the branches and render them separately
  // I tried this however, and the resulting code was harder to understand
  // maintaining a rendered branch count (while a little ugly) got me the
  // rendering I wanted with code that was reasonably understandable

  f_gen_ << indent() << "fn ok_or(self) -> thrift::Result<" << rust_return_type << "> {" << '\n';
  indent_up();

  int rendered_branch_count = 0;

  // render the exception branches
  for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
    t_field* tfield = (*members_iter);
    if (tfield->get_name() != SERVICE_RESULT_VARIABLE) { // don't have to check safe name here
      string field_name("self." + rust_field_name(tfield));
      string branch_statement = rendered_branch_count == 0 ? "if" : "} else if";

      f_gen_ << indent() << branch_statement << " " << field_name << ".is_some() {" << '\n';
      indent_up();
      f_gen_ << indent() << "Err(thrift::Error::User(Box::new(" << field_name << ".unwrap())))"
             << '\n';
      indent_down();

      rendered_branch_count++;
    }
  }

  // render the return value branches
  if (rust_return_type == "()") {
    if (rendered_branch_count == 0) {
      // we have the unit return and this service call has no user-defined
      // exceptions. this means that we've a trivial return (happens with oneways)
      f_gen_ << indent() << "Ok(())" << '\n';
    } else {
      // we have the unit return, but there are user-defined exceptions
      // if we've gotten this far then we have the default return (i.e. call successful)
      f_gen_ << indent() << "} else {" << '\n';
      indent_up();
      f_gen_ << indent() << "Ok(())" << '\n';
      indent_down();
      f_gen_ << indent() << "}" << '\n';
    }
  } else {
    string branch_statement = rendered_branch_count == 0 ? "if" : "} else if";
    f_gen_ << indent() << branch_statement << " self." << SERVICE_RESULT_VARIABLE << ".is_some() {"
           << '\n';
    indent_up();
    f_gen_ << indent() << "Ok(self." << SERVICE_RESULT_VARIABLE << ".unwrap())" << '\n';
    indent_down();
    f_gen_ << indent() << "} else {" << '\n';
    indent_up();
    // if we haven't found a valid return value *or* a user exception
    // then we're in trouble; return a default error
    render_thrift_error("Application", "ApplicationError", "ApplicationErrorKind::MissingResult",
                        "\"no result received for " + service_call_name + "\"");
    indent_down();
    f_gen_ << indent() << "}" << '\n';
  }

  indent_down();
  f_gen_ << indent() << "}" << '\n';
}

void t_rs_generator::render_union(t_struct* tstruct) {
  string union_name(rust_struct_name(tstruct));
  render_type_comment(union_name);
  render_union_definition(union_name, tstruct);
  render_union_impl(union_name, tstruct);
}

void t_rs_generator::render_union_definition(const string& union_name, t_struct* tstruct) {
  const vector<t_field*>& members = tstruct->get_sorted_members();
  if (members.empty()) {
    throw "cannot generate rust enum with 0 members"; // may be valid thrift, but it's invalid rust
  }

  f_gen_ << "#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]" << '\n';
  f_gen_ << "pub enum " << union_name << " {" << '\n';
  indent_up();

  vector<t_field*>::const_iterator member_iter;
  for (member_iter = members.begin(); member_iter != members.end(); ++member_iter) {
    t_field* tfield = (*member_iter);
    f_gen_ << indent() << rust_union_field_name(tfield) << "(" << to_rust_type(tfield->get_type())
           << ")," << '\n';
  }

  indent_down();
  f_gen_ << "}" << '\n';
  f_gen_ << '\n';
}

void t_rs_generator::render_union_impl(const string& union_name, t_struct* tstruct) {
  f_gen_ << "impl TSerializable for " << union_name << " {" << '\n';
  indent_up();

  render_union_sync_read(union_name, tstruct);
  render_union_sync_write(union_name, tstruct);

  indent_down();
  f_gen_ << "}" << '\n';
  f_gen_ << '\n';
}

//-----------------------------------------------------------------------------
//
// Sync Struct Write
//
//-----------------------------------------------------------------------------

void t_rs_generator::render_struct_sync_write(t_struct* tstruct,
                                              t_rs_generator::e_struct_type struct_type) {
  f_gen_
      << indent()
      << "fn write_to_out_protocol(&self, o_prot: &mut dyn TOutputProtocol) -> thrift::Result<()> {"
      << '\n';
  indent_up();

  // write struct header to output protocol
  // note: use the *original* struct name here
  f_gen_ << indent()
         << "let struct_ident = TStructIdentifier::new(\"" + tstruct->get_name() + "\");" << '\n';
  f_gen_ << indent() << "o_prot.write_struct_begin(&struct_ident)?;" << '\n';

  // write struct members to output protocol
  vector<t_field*> members = tstruct->get_sorted_members();
  if (!members.empty()) {
    vector<t_field*>::iterator members_iter;
    for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
      t_field* member = (*members_iter);
      t_field::e_req member_req = actual_field_req(member, struct_type);
      string member_var("self." + rust_field_name(member));
      render_struct_field_sync_write(member_var, false, member, member_req);
    }
  }

  // write struct footer to output protocol
  f_gen_ << indent() << "o_prot.write_field_stop()?;" << '\n';
  f_gen_ << indent() << "o_prot.write_struct_end()" << '\n';

  indent_down();
  f_gen_ << indent() << "}" << '\n';
}

void t_rs_generator::render_union_sync_write(const string& union_name, t_struct* tstruct) {
  f_gen_
      << indent()
      << "fn write_to_out_protocol(&self, o_prot: &mut dyn TOutputProtocol) -> thrift::Result<()> {"
      << '\n';
  indent_up();

  // write struct header to output protocol
  // note: use the *original* struct name here
  f_gen_ << indent()
         << "let struct_ident = TStructIdentifier::new(\"" + tstruct->get_name() + "\");" << '\n';
  f_gen_ << indent() << "o_prot.write_struct_begin(&struct_ident)?;" << '\n';

  // write the enum field to the output protocol
  vector<t_field*> members = tstruct->get_sorted_members();
  if (!members.empty()) {
    f_gen_ << indent() << "match *self {" << '\n';
    indent_up();
    vector<t_field*>::iterator members_iter;
    for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
      t_field* member = (*members_iter);
      t_field::e_req member_req = t_field::T_REQUIRED;
      t_type* ttype = member->get_type();
      if (ttype->is_typedef()) {
        // get the actual type of typedef
        ttype = ((t_typedef*)ttype)->get_type();
      }
      string match_var((ttype->is_base_type() && !ttype->is_string()) ? "f" : "ref f");
      f_gen_ << indent() << union_name << "::" << rust_union_field_name(member) << "(" << match_var
             << ") => {" << '\n';
      indent_up();
      render_struct_field_sync_write("f", true, member, member_req);
      indent_down();
      f_gen_ << indent() << "}," << '\n';
    }
    indent_down();
    f_gen_ << indent() << "}" << '\n';
  }

  // write struct footer to output protocol
  f_gen_ << indent() << "o_prot.write_field_stop()?;" << '\n';
  f_gen_ << indent() << "o_prot.write_struct_end()" << '\n';

  indent_down();
  f_gen_ << indent() << "}" << '\n';
}

void t_rs_generator::render_struct_field_sync_write(const string& field_var,
                                                    bool field_var_is_ref,
                                                    t_field* tfield,
                                                    t_field::e_req req) {
  t_type* field_type = tfield->get_type();
  t_type* actual_type = get_true_type(field_type);

  ostringstream field_stream;
  field_stream << "TFieldIdentifier::new("
               << "\"" << tfield->get_name() << "\""
               << ", " // note: use *original* name
               << to_rust_field_type_enum(field_type) << ", " << tfield->get_key() << ")";
  string field_ident_string = field_stream.str();

  if (is_optional(req)) {
    string let_var((actual_type->is_base_type() && !actual_type->is_string()) ? "fld_var"
                                                                              : "ref fld_var");
    f_gen_ << indent() << "if let Some(" << let_var << ") = " << field_var << " {" << '\n';
    indent_up();
    f_gen_ << indent() << "o_prot.write_field_begin(&" << field_ident_string << ")?;" << '\n';
    render_type_sync_write("fld_var", true, field_type);
    f_gen_ << indent() << "o_prot.write_field_end()?" << '\n';
    indent_down();
    /* FIXME: rethink how I deal with OPT_IN_REQ_OUT
    if (req == t_field::T_OPT_IN_REQ_OUT) {
      f_gen_ << indent() << "let field_ident = " << field_ident_string << ";" << '\n';
      f_gen_ << indent() << "o_prot.write_field_begin(&field_ident)?;" << '\n';
      f_gen_ << indent() << "o_prot.write_field_end()?;" << '\n';
    }*/
    f_gen_ << indent() << "}" << '\n';
  } else {
    f_gen_ << indent() << "o_prot.write_field_begin(&" << field_ident_string << ")?;" << '\n';
    render_type_sync_write(field_var, field_var_is_ref, tfield->get_type());
    f_gen_ << indent() << "o_prot.write_field_end()?;" << '\n';
  }
}

void t_rs_generator::render_type_sync_write(const string& type_var,
                                            bool type_var_is_ref,
                                            t_type* ttype) {
  if (ttype->is_base_type()) {
    t_base_type* tbase_type = (t_base_type*)ttype;
    switch (tbase_type->get_base()) {
    case t_base_type::TYPE_VOID:
      throw "cannot write field of type TYPE_VOID to output protocol";
    case t_base_type::TYPE_STRING: {
      string ref(type_var_is_ref ? "" : "&");
      if (tbase_type->is_binary()) {
        f_gen_ << indent() << "o_prot.write_bytes(" + ref + type_var + ")?;" << '\n';
      } else {
        f_gen_ << indent() << "o_prot.write_string(" + ref + type_var + ")?;" << '\n';
      }
      return;
    }
    case t_base_type::TYPE_UUID:
      f_gen_ << indent() << "o_prot.write_uuid(&" + type_var + ")?;" << '\n';
      return;
    case t_base_type::TYPE_BOOL:
      f_gen_ << indent() << "o_prot.write_bool(" + type_var + ")?;" << '\n';
      return;
    case t_base_type::TYPE_I8:
      f_gen_ << indent() << "o_prot.write_i8(" + type_var + ")?;" << '\n';
      return;
    case t_base_type::TYPE_I16:
      f_gen_ << indent() << "o_prot.write_i16(" + type_var + ")?;" << '\n';
      return;
    case t_base_type::TYPE_I32:
      f_gen_ << indent() << "o_prot.write_i32(" + type_var + ")?;" << '\n';
      return;
    case t_base_type::TYPE_I64:
      f_gen_ << indent() << "o_prot.write_i64(" + type_var + ")?;" << '\n';
      return;
    case t_base_type::TYPE_DOUBLE:
      f_gen_ << indent() << "o_prot.write_double(" + type_var + ".into())?;" << '\n';
      return;
    default:
      throw "compiler error: unhandled type";
    }
  } else if (ttype->is_typedef()) {
    t_typedef* ttypedef = (t_typedef*)ttype;
    render_type_sync_write(type_var, type_var_is_ref, ttypedef->get_type());
    return;
  } else if (ttype->is_enum() || ttype->is_struct() || ttype->is_xception()) {
    f_gen_ << indent() << type_var + ".write_to_out_protocol(o_prot)?;" << '\n';
    return;
  } else if (ttype->is_map()) {
    render_map_sync_write(type_var, type_var_is_ref, (t_map*)ttype);
    return;
  } else if (ttype->is_set()) {
    render_set_sync_write(type_var, type_var_is_ref, (t_set*)ttype);
    return;
  } else if (ttype->is_list()) {
    render_list_sync_write(type_var, type_var_is_ref, (t_list*)ttype);
    return;
  }

  throw "cannot write unsupported type " + ttype->get_name();
}

void t_rs_generator::render_list_sync_write(const string& list_var,
                                            bool list_var_is_ref,
                                            t_list* tlist) {
  t_type* elem_type = tlist->get_elem_type();

  f_gen_ << indent() << "o_prot.write_list_begin("
         << "&TListIdentifier::new(" << to_rust_field_type_enum(elem_type) << ", " << list_var
         << ".len() as i32"
         << ")"
         << ")?;" << '\n';

  string ref(list_var_is_ref ? "" : "&");
  f_gen_ << indent() << "for e in " << ref << list_var << " {" << '\n';
  indent_up();
  render_type_sync_write(string_container_write_variable(elem_type, "e"), true, elem_type);
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  f_gen_ << indent() << "o_prot.write_list_end()?;" << '\n';
}

void t_rs_generator::render_set_sync_write(const string& set_var,
                                           bool set_var_is_ref,
                                           t_set* tset) {
  t_type* elem_type = tset->get_elem_type();

  f_gen_ << indent() << "o_prot.write_set_begin("
         << "&TSetIdentifier::new(" << to_rust_field_type_enum(elem_type) << ", " << set_var
         << ".len() as i32"
         << ")"
         << ")?;" << '\n';

  string ref(set_var_is_ref ? "" : "&");
  f_gen_ << indent() << "for e in " << ref << set_var << " {" << '\n';
  indent_up();
  render_type_sync_write(string_container_write_variable(elem_type, "e"), true, elem_type);
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  f_gen_ << indent() << "o_prot.write_set_end()?;" << '\n';
}

void t_rs_generator::render_map_sync_write(const string& map_var,
                                           bool map_var_is_ref,
                                           t_map* tmap) {
  t_type* key_type = tmap->get_key_type();
  t_type* val_type = tmap->get_val_type();

  f_gen_ << indent() << "o_prot.write_map_begin("
         << "&TMapIdentifier::new(" << to_rust_field_type_enum(key_type) << ", "
         << to_rust_field_type_enum(val_type) << ", " << map_var << ".len() as i32)"
         << ")?;" << '\n';

  string ref(map_var_is_ref ? "" : "&");
  f_gen_ << indent() << "for (k, v) in " << ref << map_var << " {" << '\n';
  indent_up();
  render_type_sync_write(string_container_write_variable(key_type, "k"), true, key_type);
  render_type_sync_write(string_container_write_variable(val_type, "v"), true, val_type);
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  f_gen_ << indent() << "o_prot.write_map_end()?;" << '\n';
}

string t_rs_generator::string_container_write_variable(t_type* ttype, const string& base_var) {
  bool type_needs_deref = needs_deref_on_container_write(ttype);
  bool type_is_double = is_double(ttype);

  string write_variable;

  if (type_is_double && type_needs_deref) {
    write_variable = "(*" + base_var + ")";
  } else if (type_needs_deref) {
    write_variable = "*" + base_var;
  } else {
    write_variable = base_var;
  }

  return write_variable;
}

bool t_rs_generator::needs_deref_on_container_write(t_type* ttype) {
  ttype = get_true_type(ttype);
  return ttype->is_base_type() && !ttype->is_string();
}

//-----------------------------------------------------------------------------
//
// Sync Struct Read
//
//-----------------------------------------------------------------------------

void t_rs_generator::render_struct_sync_read(const string& struct_name,
                                             t_struct* tstruct,
                                             t_rs_generator::e_struct_type struct_type) {
  f_gen_ << indent()
         << "fn read_from_in_protocol(i_prot: &mut dyn TInputProtocol) -> thrift::Result<"
         << struct_name << "> {" << '\n';

  indent_up();

  f_gen_ << indent() << "i_prot.read_struct_begin()?;" << '\n';

  // create temporary variables: one for each field in the struct
  const vector<t_field*> members = tstruct->get_sorted_members();
  vector<t_field*>::const_iterator members_iter;
  for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
    t_field* member = (*members_iter);
    t_field::e_req member_req = actual_field_req(member, struct_type);

    f_gen_ << indent() << "let mut " << struct_field_read_temp_variable(member) << ": Option<"
           << to_rust_type(member->get_type()) << "> = ";
    if (member_req == t_field::T_OPT_IN_REQ_OUT) {
      f_gen_ << opt_in_req_out_value(member->get_type()) << ";";
    } else {
      f_gen_ << "None;";
    }
    f_gen_ << '\n';
  }

  // now loop through the fields we've received
  f_gen_ << indent() << "loop {" << '\n'; // start loop
  indent_up();

  // break out if you've found the Stop field
  f_gen_ << indent() << "let field_ident = i_prot.read_field_begin()?;" << '\n';
  f_gen_ << indent() << "if field_ident.field_type == TType::Stop {" << '\n';
  indent_up();
  f_gen_ << indent() << "break;" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';

  // now read all the fields found
  // avoid clippy::match_single_binding
  if (members.empty()) {
    f_gen_ << indent() << "i_prot.skip(field_ident.field_type)?;" << '\n';
  } else {
    f_gen_ << indent() << "let field_id = field_id(&field_ident)?;" << '\n';
    f_gen_ << indent() << "match field_id {" << '\n'; // start match
    indent_up();

    for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
      t_field* tfield = (*members_iter);
      f_gen_ << indent() << rust_safe_field_id(tfield->get_key()) << " => {" << '\n';
      indent_up();
      render_type_sync_read("val", tfield->get_type());
      f_gen_ << indent() << struct_field_read_temp_variable(tfield) << " = Some(val);" << '\n';
      indent_down();
      f_gen_ << indent() << "}," << '\n';
    }

    // default case (skip fields)
    f_gen_ << indent() << "_ => {" << '\n';
    indent_up();
    f_gen_ << indent() << "i_prot.skip(field_ident.field_type)?;" << '\n';
    indent_down();
    f_gen_ << indent() << "}," << '\n';

    indent_down();
    f_gen_ << indent() << "};" << '\n'; // finish match
  }

  f_gen_ << indent() << "i_prot.read_field_end()?;" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';                          // finish loop
  f_gen_ << indent() << "i_prot.read_struct_end()?;" << '\n'; // read message footer from the wire

  // verify that all required fields exist
  for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
    t_field* tfield = (*members_iter);
    t_field::e_req req = actual_field_req(tfield, struct_type);
    if (!is_optional(req)) {
      f_gen_ << indent() << "verify_required_field_exists("
             << "\"" << struct_name << "." << rust_field_name(tfield) << "\""
             << ", "
             << "&" << struct_field_read_temp_variable(tfield) << ")?;" << '\n';
    }
  }

  // construct the struct
  if (members.size() == 0) {
    f_gen_ << indent() << "let ret = " << struct_name << " {};" << '\n';
  } else {
    f_gen_ << indent() << "let ret = " << struct_name << " {" << '\n';
    indent_up();

    for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
      t_field* tfield = (*members_iter);
      t_field::e_req req = actual_field_req(tfield, struct_type);
      string field_name(rust_field_name(tfield));
      string field_key = struct_field_read_temp_variable(tfield);
      if (is_optional(req)) {
        f_gen_ << indent() << field_name << ": " << field_key << "," << '\n';
      } else {
        f_gen_ << indent() << field_name << ": " << field_key
               << ".expect(\"auto-generated code should have checked for presence of required "
                  "fields\")"
               << "," << '\n';
      }
    }

    indent_down();
    f_gen_ << indent() << "};" << '\n';
  }

  // return the constructed value
  f_gen_ << indent() << "Ok(ret)" << '\n';

  indent_down();
  f_gen_ << indent() << "}" << '\n';
}

void t_rs_generator::render_union_sync_read(const string& union_name, t_struct* tstruct) {
  f_gen_ << indent()
         << "fn read_from_in_protocol(i_prot: &mut dyn TInputProtocol) -> thrift::Result<"
         << union_name << "> {" << '\n';
  indent_up();

  // create temporary variables to hold the
  // completed union as well as a count of fields read
  f_gen_ << indent() << "let mut ret: Option<" << union_name << "> = None;" << '\n';
  f_gen_ << indent() << "let mut received_field_count = 0;" << '\n';

  // read the struct preamble
  f_gen_ << indent() << "i_prot.read_struct_begin()?;" << '\n';

  // now loop through the fields we've received
  f_gen_ << indent() << "loop {" << '\n'; // start loop
  indent_up();

  // break out if you've found the Stop field
  f_gen_ << indent() << "let field_ident = i_prot.read_field_begin()?;" << '\n';
  f_gen_ << indent() << "if field_ident.field_type == TType::Stop {" << '\n';
  indent_up();
  f_gen_ << indent() << "break;" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';

  // now read all the fields found
  f_gen_ << indent() << "let field_id = field_id(&field_ident)?;" << '\n';
  f_gen_ << indent() << "match field_id {" << '\n'; // start match
  indent_up();

  const vector<t_field*> members = tstruct->get_sorted_members();
  vector<t_field*>::const_iterator members_iter;
  for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
    t_field* member = (*members_iter);
    f_gen_ << indent() << rust_safe_field_id(member->get_key()) << " => {" << '\n';
    indent_up();
    render_type_sync_read("val", member->get_type());
    f_gen_ << indent() << "if ret.is_none() {" << '\n';
    indent_up();
    f_gen_ << indent() << "ret = Some(" << union_name << "::" << rust_union_field_name(member)
           << "(val));" << '\n';
    indent_down();
    f_gen_ << indent() << "}" << '\n';
    f_gen_ << indent() << "received_field_count += 1;" << '\n';
    indent_down();
    f_gen_ << indent() << "}," << '\n';
  }

  // default case (skip fields)
  f_gen_ << indent() << "_ => {" << '\n';
  indent_up();
  f_gen_ << indent() << "i_prot.skip(field_ident.field_type)?;" << '\n';
  f_gen_ << indent() << "received_field_count += 1;" << '\n';
  indent_down();
  f_gen_ << indent() << "}," << '\n';

  indent_down();
  f_gen_ << indent() << "};" << '\n'; // finish match
  f_gen_ << indent() << "i_prot.read_field_end()?;" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';                          // finish loop
  f_gen_ << indent() << "i_prot.read_struct_end()?;" << '\n'; // finish reading message from wire

  // return the value or an error
  f_gen_ << indent() << "if received_field_count == 0 {" << '\n';
  indent_up();
  render_thrift_error("Protocol", "ProtocolError", "ProtocolErrorKind::InvalidData",
                      "\"received empty union from remote " + union_name + "\"");
  indent_down();
  f_gen_ << indent() << "} else if received_field_count > 1 {" << '\n';
  indent_up();
  render_thrift_error("Protocol", "ProtocolError", "ProtocolErrorKind::InvalidData",
                      "\"received multiple fields for union from remote " + union_name + "\"");
  indent_down();
  f_gen_ << indent() << "} else if let Some(ret) = ret {" << '\n';
  indent_up();
  f_gen_ << indent() << "Ok(ret)" << '\n';
  indent_down();
  f_gen_ << indent() << "} else {" << '\n';
  indent_up();
  f_gen_ << indent() << "Err(" << '\n';
  indent_up();
  f_gen_ << indent() << "thrift::Error::Protocol(" << '\n';
  f_gen_ << indent() << "  ProtocolError::new(ProtocolErrorKind::InvalidData, \"return value should have been constructed\")" << '\n';
  f_gen_ << indent() << ")" << '\n';
  indent_down();
  f_gen_ << indent() << ")" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';

  indent_down();
  f_gen_ << indent() << "}" << '\n';
}

// Construct the rust representation of all supported types from the wire.
void t_rs_generator::render_type_sync_read(const string& type_var, t_type* ttype, bool is_boxed) {
  if (ttype->is_base_type()) {
    t_base_type* tbase_type = (t_base_type*)ttype;
    switch (tbase_type->get_base()) {
    case t_base_type::TYPE_VOID:
      throw "cannot read field of type TYPE_VOID from input protocol";
    case t_base_type::TYPE_STRING:
      if (tbase_type->is_binary()) {
        f_gen_ << indent() << "let " << type_var << " = i_prot.read_bytes()?;" << '\n';
      } else {
        f_gen_ << indent() << "let " << type_var << " = i_prot.read_string()?;" << '\n';
      }
      return;
    case t_base_type::TYPE_UUID:
      f_gen_ << indent() << "let " << type_var << " = i_prot.read_uuid()?;" << '\n';
      return;
    case t_base_type::TYPE_BOOL:
      f_gen_ << indent() << "let " << type_var << " = i_prot.read_bool()?;" << '\n';
      return;
    case t_base_type::TYPE_I8:
      f_gen_ << indent() << "let " << type_var << " = i_prot.read_i8()?;" << '\n';
      return;
    case t_base_type::TYPE_I16:
      f_gen_ << indent() << "let " << type_var << " = i_prot.read_i16()?;" << '\n';
      return;
    case t_base_type::TYPE_I32:
      f_gen_ << indent() << "let " << type_var << " = i_prot.read_i32()?;" << '\n';
      return;
    case t_base_type::TYPE_I64:
      f_gen_ << indent() << "let " << type_var << " = i_prot.read_i64()?;" << '\n';
      return;
    case t_base_type::TYPE_DOUBLE:
      f_gen_ << indent() << "let " << type_var << " = OrderedFloat::from(i_prot.read_double()?);"
             << '\n';
      return;
    default:
      throw "compiler error: unhandled type";
    }
  } else if (ttype->is_typedef()) {
    // FIXME: not a fan of separate `is_boxed` parameter
    // This is problematic because it's an optional parameter, and only comes
    // into play once. The core issue is that I lose an important piece of type
    // information (whether the type is a fwd ref) by unwrapping the typedef'd
    // type and making the recursive call using it. I can't modify or wrap the
    // generated string after the fact because it's written directly into the file,
    // so I have to pass this parameter along. Going with this approach because it
    // seems like the lowest-cost option to easily support recursive types.
    t_typedef* ttypedef = (t_typedef*)ttype;
    render_type_sync_read(type_var, ttypedef->get_type(), ttypedef->is_forward_typedef());
    return;
  } else if (ttype->is_enum() || ttype->is_struct() || ttype->is_xception()) {
    string read_call(to_rust_type(ttype) + "::read_from_in_protocol(i_prot)?");
    read_call = is_boxed ? "Box::new(" + read_call + ")" : read_call;
    f_gen_ << indent() << "let " << type_var << " = " << read_call << ";" << '\n';
    return;
  } else if (ttype->is_map()) {
    render_map_sync_read((t_map*)ttype, type_var);
    return;
  } else if (ttype->is_set()) {
    render_set_sync_read((t_set*)ttype, type_var);
    return;
  } else if (ttype->is_list()) {
    render_list_sync_read((t_list*)ttype, type_var);
    return;
  }

  throw "cannot read unsupported type " + ttype->get_name();
}

// Construct the rust representation of a list from the wire.
void t_rs_generator::render_list_sync_read(t_list* tlist, const string& list_var) {
  t_type* elem_type = tlist->get_elem_type();

  f_gen_ << indent() << "let list_ident = i_prot.read_list_begin()?;" << '\n';
  f_gen_ << indent() << "let mut " << list_var << ": " << to_rust_type((t_type*)tlist)
         << " = Vec::with_capacity(list_ident.size as usize);" << '\n';
  f_gen_ << indent() << "for _ in 0..list_ident.size {" << '\n';

  indent_up();

  string list_elem_var = tmp("list_elem_");
  render_type_sync_read(list_elem_var, elem_type);
  f_gen_ << indent() << list_var << ".push(" << list_elem_var << ");" << '\n';

  indent_down();

  f_gen_ << indent() << "}" << '\n';
  f_gen_ << indent() << "i_prot.read_list_end()?;" << '\n';
}

// Construct the rust representation of a set from the wire.
void t_rs_generator::render_set_sync_read(t_set* tset, const string& set_var) {
  t_type* elem_type = tset->get_elem_type();

  f_gen_ << indent() << "let set_ident = i_prot.read_set_begin()?;" << '\n';
  f_gen_ << indent() << "let mut " << set_var << ": " << to_rust_type((t_type*)tset)
         << " = BTreeSet::new();" << '\n';
  f_gen_ << indent() << "for _ in 0..set_ident.size {" << '\n';

  indent_up();

  string set_elem_var = tmp("set_elem_");
  render_type_sync_read(set_elem_var, elem_type);
  f_gen_ << indent() << set_var << ".insert(" << set_elem_var << ");" << '\n';

  indent_down();

  f_gen_ << indent() << "}" << '\n';
  f_gen_ << indent() << "i_prot.read_set_end()?;" << '\n';
}

// Construct the rust representation of a map from the wire.
void t_rs_generator::render_map_sync_read(t_map* tmap, const string& map_var) {
  t_type* key_type = tmap->get_key_type();
  t_type* val_type = tmap->get_val_type();

  f_gen_ << indent() << "let map_ident = i_prot.read_map_begin()?;" << '\n';
  f_gen_ << indent() << "let mut " << map_var << ": " << to_rust_type((t_type*)tmap)
         << " = BTreeMap::new();" << '\n';
  f_gen_ << indent() << "for _ in 0..map_ident.size {" << '\n';

  indent_up();

  string key_elem_var = tmp("map_key_");
  render_type_sync_read(key_elem_var, key_type);
  string val_elem_var = tmp("map_val_");
  render_type_sync_read(val_elem_var, val_type);
  f_gen_ << indent() << map_var << ".insert(" << key_elem_var << ", " << val_elem_var << ");"
         << '\n';

  indent_down();

  f_gen_ << indent() << "}" << '\n';
  f_gen_ << indent() << "i_prot.read_map_end()?;" << '\n';
}

string t_rs_generator::struct_field_read_temp_variable(t_field* tfield) {
  std::ostringstream foss;
  foss << "f_" << rust_safe_field_id(tfield->get_key());
  return foss.str();
}

//-----------------------------------------------------------------------------
//
// Sync Client
//
//-----------------------------------------------------------------------------

void t_rs_generator::generate_service(t_service* tservice) {
  render_sync_client(tservice);
  render_sync_processor(tservice);
  render_service_call_structs(tservice);
}

void t_rs_generator::render_service_call_structs(t_service* tservice) {
  const std::vector<t_function*> functions = tservice->get_functions();
  std::vector<t_function*>::const_iterator func_iter;

  // thrift args for service calls are packed
  // into a struct that's transmitted over the wire, so
  // generate structs for those too
  //
  // thrift returns are *also* packed into a struct
  // that's passed over the wire, so, generate the struct
  // for that too. Note that this result struct *also*
  // contains the exceptions as well
  for (func_iter = functions.begin(); func_iter != functions.end(); ++func_iter) {
    t_function* tfunc = (*func_iter);
    render_service_call_args_struct(tfunc);
    if (!tfunc->is_oneway()) {
      render_service_call_result_value_struct(tfunc);
    }
  }
}

void t_rs_generator::render_sync_client(t_service* tservice) {
  string client_impl_name(rust_sync_client_impl_name(tservice));

  render_type_comment(tservice->get_name() + " service client"); // note: use *original* name
  render_sync_client_trait(tservice);
  render_sync_client_marker_trait(tservice);
  render_sync_client_definition_and_impl(client_impl_name);
  render_sync_client_tthriftclient_impl(client_impl_name);
  render_sync_client_marker_trait_impls(tservice, client_impl_name);
  f_gen_ << '\n';
  render_sync_client_process_impl(tservice);
}

void t_rs_generator::render_sync_client_trait(t_service* tservice) {
  string extension = "";
  if (tservice->get_extends()) {
    t_service* extends = tservice->get_extends();
    extension = " : " + rust_namespace(extends) + rust_sync_client_trait_name(extends);
  }

  render_rustdoc((t_doc*)tservice);
  f_gen_ << "pub trait " << rust_sync_client_trait_name(tservice) << extension << " {" << '\n';
  indent_up();

  const std::vector<t_function*> functions = tservice->get_functions();
  std::vector<t_function*>::const_iterator func_iter;
  for (func_iter = functions.begin(); func_iter != functions.end(); ++func_iter) {
    t_function* tfunc = (*func_iter);
    string func_name = service_call_client_function_name(tfunc);
    string func_args = rust_sync_service_call_declaration(tfunc, true);
    string func_return = to_rust_type(tfunc->get_returntype());
    render_rustdoc((t_doc*)tfunc);
    f_gen_ << indent() << "fn " << func_name << func_args << " -> thrift::Result<" << func_return
           << ">;" << '\n';
  }

  indent_down();
  f_gen_ << indent() << "}" << '\n';
  f_gen_ << '\n';
}

void t_rs_generator::render_sync_client_marker_trait(t_service* tservice) {
  f_gen_ << indent() << "pub trait " << rust_sync_client_marker_trait_name(tservice) << " {}"
         << '\n';
  f_gen_ << '\n';
}

void t_rs_generator::render_sync_client_marker_trait_impls(t_service* tservice,
                                                           const string& impl_struct_name) {
  f_gen_ << indent() << "impl " << SYNC_CLIENT_GENERIC_BOUND_VARS << " " << rust_namespace(tservice)
         << rust_sync_client_marker_trait_name(tservice) << " for " << impl_struct_name
         << SYNC_CLIENT_GENERIC_BOUND_VARS << " " << SYNC_CLIENT_GENERIC_BOUNDS << " {}" << '\n';

  t_service* extends = tservice->get_extends();
  if (extends) {
    render_sync_client_marker_trait_impls(extends, impl_struct_name);
  }
}

void t_rs_generator::render_sync_client_definition_and_impl(const string& client_impl_name) {

  // render the definition for the client struct
  f_gen_ << "pub struct " << client_impl_name << SYNC_CLIENT_GENERIC_BOUND_VARS << " "
         << SYNC_CLIENT_GENERIC_BOUNDS << " {" << '\n';
  indent_up();
  f_gen_ << indent() << "_i_prot: IP," << '\n';
  f_gen_ << indent() << "_o_prot: OP," << '\n';
  f_gen_ << indent() << "_sequence_number: i32," << '\n';
  indent_down();
  f_gen_ << "}" << '\n';
  f_gen_ << '\n';

  // render the struct implementation
  // this includes the new() function as well as the helper send/recv methods for each service call
  f_gen_ << "impl " << SYNC_CLIENT_GENERIC_BOUND_VARS << " " << client_impl_name
         << SYNC_CLIENT_GENERIC_BOUND_VARS << " " << SYNC_CLIENT_GENERIC_BOUNDS << " {" << '\n';
  indent_up();
  render_sync_client_lifecycle_functions(client_impl_name);
  indent_down();
  f_gen_ << "}" << '\n';
  f_gen_ << '\n';
}

void t_rs_generator::render_sync_client_lifecycle_functions(const string& client_struct) {
  f_gen_ << indent() << "pub fn new(input_protocol: IP, output_protocol: OP) -> " << client_struct
         << SYNC_CLIENT_GENERIC_BOUND_VARS << " {" << '\n';
  indent_up();

  f_gen_ << indent() << client_struct
         << " { _i_prot: input_protocol, _o_prot: output_protocol, _sequence_number: 0 }" << '\n';

  indent_down();
  f_gen_ << indent() << "}" << '\n';
}

void t_rs_generator::render_sync_client_tthriftclient_impl(const string& client_impl_name) {
  f_gen_ << indent() << "impl " << SYNC_CLIENT_GENERIC_BOUND_VARS << " TThriftClient for "
         << client_impl_name << SYNC_CLIENT_GENERIC_BOUND_VARS << " " << SYNC_CLIENT_GENERIC_BOUNDS
         << " {" << '\n';
  indent_up();

  f_gen_ << indent() << "fn i_prot_mut(&mut self) -> &mut dyn TInputProtocol { &mut self._i_prot }"
         << '\n';
  f_gen_ << indent() << "fn o_prot_mut(&mut self) -> &mut dyn TOutputProtocol { &mut self._o_prot }"
         << '\n';
  f_gen_ << indent() << "fn sequence_number(&self) -> i32 { self._sequence_number }" << '\n';
  f_gen_ << indent()
         << "fn increment_sequence_number(&mut self) -> i32 { self._sequence_number += 1; "
            "self._sequence_number }"
         << '\n';

  indent_down();
  f_gen_ << indent() << "}" << '\n';
  f_gen_ << '\n';
}

void t_rs_generator::render_sync_client_process_impl(t_service* tservice) {
  string marker_extension = "" + sync_client_marker_traits_for_extension(tservice);

  f_gen_ << "impl <C: TThriftClient + " << rust_sync_client_marker_trait_name(tservice)
         << marker_extension << "> " << rust_sync_client_trait_name(tservice) << " for C {" << '\n';
  indent_up();

  const std::vector<t_function*> functions = tservice->get_functions();
  std::vector<t_function*>::const_iterator func_iter;
  for (func_iter = functions.begin(); func_iter != functions.end(); ++func_iter) {
    t_function* func = (*func_iter);
    render_sync_send_recv_wrapper(func);
  }

  indent_down();
  f_gen_ << "}" << '\n';
  f_gen_ << '\n';
}

string t_rs_generator::sync_client_marker_traits_for_extension(t_service* tservice) {
  string marker_extension;

  t_service* extends = tservice->get_extends();
  if (extends) {
    marker_extension
        = " + " + rust_namespace(extends) + rust_sync_client_marker_trait_name(extends);
    marker_extension = marker_extension + sync_client_marker_traits_for_extension(extends);
  }

  return marker_extension;
}

void t_rs_generator::render_sync_send_recv_wrapper(t_function* tfunc) {
  string func_name = service_call_client_function_name(tfunc);
  string func_decl_args = rust_sync_service_call_declaration(tfunc, true);
  string func_call_args = rust_sync_service_call_invocation(tfunc);
  string func_return = to_rust_type(tfunc->get_returntype());

  f_gen_ << indent() << "fn " << func_name << func_decl_args << " -> thrift::Result<" << func_return
         << "> {" << '\n';
  indent_up();

  f_gen_ << indent() << "(" << '\n';
  indent_up();
  render_sync_send(tfunc);
  indent_down();
  f_gen_ << indent() << ")?;" << '\n';
  if (tfunc->is_oneway()) {
    f_gen_ << indent() << "Ok(())" << '\n';
  } else {
    render_sync_recv(tfunc);
  }

  indent_down();
  f_gen_ << indent() << "}" << '\n';
}

void t_rs_generator::render_sync_send(t_function* tfunc) {
  f_gen_ << indent() << "{" << '\n';
  indent_up();

  // increment the sequence number and generate the call header
  string message_type = tfunc->is_oneway() ? "TMessageType::OneWay" : "TMessageType::Call";
  f_gen_ << indent() << "self.increment_sequence_number();" << '\n';
  f_gen_ << indent() << "let message_ident = "
         << "TMessageIdentifier::new(\"" << tfunc->get_name() << "\", " // note: use *original* name
         << message_type << ", "
         << "self.sequence_number());" << '\n';
  // pack the arguments into the containing struct that we'll write out over the wire
  // note that this struct is generated even if we have 0 args
  ostringstream struct_definition;
  vector<t_field*> members = tfunc->get_arglist()->get_sorted_members();
  vector<t_field*>::iterator members_iter;
  for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
    t_field* member = (*members_iter);
    string member_name(rust_field_name(member));
    struct_definition << member_name << ", ";
  }
  string struct_fields = struct_definition.str();
  if (struct_fields.size() > 0) {
    struct_fields = struct_fields.substr(0, struct_fields.size() - 2); // strip trailing comma
  }
  f_gen_ << indent() << "let call_args = " << service_call_args_struct_name(tfunc) << " { "
         << struct_fields << " };" << '\n';
  // write everything over the wire
  f_gen_ << indent() << "self.o_prot_mut().write_message_begin(&message_ident)?;" << '\n';
  f_gen_ << indent() << "call_args.write_to_out_protocol(self.o_prot_mut())?;"
         << '\n'; // written even if we have 0 args
  f_gen_ << indent() << "self.o_prot_mut().write_message_end()?;" << '\n';
  f_gen_ << indent() << "self.o_prot_mut().flush()" << '\n';

  indent_down();
  f_gen_ << indent() << "}" << '\n';
}

void t_rs_generator::render_sync_recv(t_function* tfunc) {
  f_gen_ << indent() << "{" << '\n';
  indent_up();

  f_gen_ << indent() << "let message_ident = self.i_prot_mut().read_message_begin()?;" << '\n';
  f_gen_
      << indent()
      << "verify_expected_sequence_number(self.sequence_number(), message_ident.sequence_number)?;"
      << '\n';
  f_gen_ << indent() << "verify_expected_service_call(\"" << tfunc->get_name()
         << "\", &message_ident.name)?;" << '\n'; // note: use *original* name
  // FIXME: replace with a "try" block
  f_gen_ << indent() << "if message_ident.message_type == TMessageType::Exception {" << '\n';
  indent_up();
  f_gen_ << indent()
         << "let remote_error = "
            "thrift::Error::read_application_error_from_in_protocol(self.i_prot_mut())?;"
         << '\n';
  f_gen_ << indent() << "self.i_prot_mut().read_message_end()?;" << '\n';
  f_gen_ << indent() << "return Err(thrift::Error::Application(remote_error))" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  f_gen_ << indent()
         << "verify_expected_message_type(TMessageType::Reply, message_ident.message_type)?;"
         << '\n';
  f_gen_ << indent() << "let result = " << service_call_result_struct_name(tfunc)
         << "::read_from_in_protocol(self.i_prot_mut())?;" << '\n';
  f_gen_ << indent() << "self.i_prot_mut().read_message_end()?;" << '\n';
  f_gen_ << indent() << "result.ok_or()" << '\n';

  indent_down();
  f_gen_ << indent() << "}" << '\n';
}

string t_rs_generator::rust_sync_service_call_declaration(t_function* tfunc, bool self_is_mutable) {
  ostringstream func_args;

  if (self_is_mutable) {
    func_args << "(&mut self";
  } else {
    func_args << "(&self";
  }

  if (has_args(tfunc)) {
    func_args << ", "; // put comma after "self"
    func_args << struct_to_declaration(tfunc->get_arglist(), T_ARGS);
  }

  func_args << ")";
  return func_args.str();
}

string t_rs_generator::rust_sync_service_call_invocation(t_function* tfunc,
                                                         const string& field_prefix) {
  ostringstream func_args;
  func_args << "(";

  if (has_args(tfunc)) {
    func_args << struct_to_invocation(tfunc->get_arglist(), field_prefix);
  }

  func_args << ")";
  return func_args.str();
}

string t_rs_generator::struct_to_declaration(t_struct* tstruct,
                                             t_rs_generator::e_struct_type struct_type) {
  ostringstream args;

  bool first_arg = true;
  std::vector<t_field*> fields = tstruct->get_sorted_members();
  std::vector<t_field*>::iterator field_iter;
  for (field_iter = fields.begin(); field_iter != fields.end(); ++field_iter) {
    t_field* tfield = (*field_iter);
    t_field::e_req field_req = actual_field_req(tfield, struct_type);
    string rust_type = to_rust_type(tfield->get_type());
    rust_type = is_optional(field_req) ? "Option<" + rust_type + ">" : rust_type;

    if (first_arg) {
      first_arg = false;
    } else {
      args << ", ";
    }

    args << rust_field_name(tfield) << ": " << rust_type;
  }

  return args.str();
}

string t_rs_generator::struct_to_invocation(t_struct* tstruct, const string& field_prefix) {
  ostringstream args;

  bool first_arg = true;
  std::vector<t_field*> fields = tstruct->get_sorted_members();
  std::vector<t_field*>::iterator field_iter;
  for (field_iter = fields.begin(); field_iter != fields.end(); ++field_iter) {
    t_field* tfield = (*field_iter);

    if (first_arg) {
      first_arg = false;
    } else {
      args << ", ";
    }

    args << field_prefix << rust_field_name(tfield);
  }

  return args.str();
}

void t_rs_generator::render_service_call_args_struct(t_function* tfunc) {
  string args_struct_name(service_call_args_struct_name(tfunc));
  render_struct(args_struct_name, tfunc->get_arglist(), t_rs_generator::T_ARGS);
}

void t_rs_generator::render_service_call_result_value_struct(t_function* tfunc) {
  string result_struct_name = service_call_result_struct_name(tfunc);
  t_struct result(program_, result_struct_name);

  t_field return_value(tfunc->get_returntype(), SERVICE_RESULT_VARIABLE, 0);
  return_value.set_req(t_field::T_OPTIONAL);
  if (!tfunc->get_returntype()->is_void()) {
    result.append(&return_value);
  }

  t_struct* exceptions = tfunc->get_xceptions();
  const vector<t_field*>& exception_types = exceptions->get_members();
  vector<t_field*>::const_iterator exception_iter;
  for (exception_iter = exception_types.begin(); exception_iter != exception_types.end();
       ++exception_iter) {
    t_field* exception_type = *exception_iter;
    exception_type->set_req(t_field::T_OPTIONAL);
    result.append(exception_type);
  }

  render_struct(result_struct_name, &result, t_rs_generator::T_RESULT);
}

//-----------------------------------------------------------------------------
//
// Sync Processor
//
//-----------------------------------------------------------------------------

void t_rs_generator::render_sync_processor(t_service* tservice) {
  render_type_comment(tservice->get_name() + " service processor"); // note: use *original* name
  render_sync_handler_trait(tservice);
  render_sync_processor_definition_and_impl(tservice);
}

void t_rs_generator::render_sync_handler_trait(t_service* tservice) {
  string extension = "";
  if (tservice->get_extends() != nullptr) {
    t_service* extends = tservice->get_extends();
    extension = " : " + rust_namespace(extends) + rust_sync_handler_trait_name(extends);
  }

  const std::vector<t_function*> functions = tservice->get_functions();
  std::vector<t_function*>::const_iterator func_iter;

  render_rustdoc((t_doc*)tservice);
  f_gen_ << "pub trait " << rust_sync_handler_trait_name(tservice) << extension << " {" << '\n';
  indent_up();
  for (func_iter = functions.begin(); func_iter != functions.end(); ++func_iter) {
    t_function* tfunc = (*func_iter);
    string func_name = service_call_handler_function_name(tfunc);
    string func_args = rust_sync_service_call_declaration(tfunc, false);
    string func_return = to_rust_type(tfunc->get_returntype());
    render_rustdoc((t_doc*)tfunc);
    f_gen_ << indent() << "fn " << func_name << func_args << " -> thrift::Result<" << func_return
           << ">;" << '\n';
  }
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  f_gen_ << '\n';
}

void t_rs_generator::render_sync_processor_definition_and_impl(t_service* tservice) {
  string service_processor_name = rust_sync_processor_name(tservice);
  string handler_trait_name = rust_sync_handler_trait_name(tservice);

  // struct
  f_gen_ << indent() << "pub struct " << service_processor_name << "<H: " << handler_trait_name
         << "> {" << '\n';
  indent_up();
  f_gen_ << indent() << "handler: H," << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  f_gen_ << '\n';

  // delegating impl
  f_gen_ << indent() << "impl <H: " << handler_trait_name << "> " << service_processor_name
         << "<H> {" << '\n';
  indent_up();
  f_gen_ << indent() << "pub fn new(handler: H) -> " << service_processor_name << "<H> {" << '\n';
  indent_up();
  f_gen_ << indent() << service_processor_name << " {" << '\n';
  indent_up();
  f_gen_ << indent() << "handler," << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  render_sync_process_delegation_functions(tservice);
  indent_down();
  f_gen_ << indent() << "}" << '\n';
  f_gen_ << '\n';

  // actual impl
  string service_actual_processor_name = rust_sync_processor_impl_name(tservice);
  f_gen_ << indent() << "pub struct " << service_actual_processor_name << ";" << '\n';
  f_gen_ << '\n';
  f_gen_ << indent() << "impl " << service_actual_processor_name << " {" << '\n';
  indent_up();

  vector<t_function*> functions = tservice->get_functions();
  vector<t_function*>::iterator func_iter;
  for (func_iter = functions.begin(); func_iter != functions.end(); ++func_iter) {
    t_function* tfunc = (*func_iter);
    render_sync_process_function(tfunc, handler_trait_name);
  }

  indent_down();
  f_gen_ << indent() << "}" << '\n';
  f_gen_ << '\n';

  // processor impl
  f_gen_ << indent() << "impl <H: " << handler_trait_name << "> TProcessor for "
         << service_processor_name << "<H> {" << '\n';
  indent_up();

  f_gen_ << indent()
         << "fn process(&self, i_prot: &mut dyn TInputProtocol, o_prot: &mut dyn TOutputProtocol) "
            "-> thrift::Result<()> {"
         << '\n';
  indent_up();

  f_gen_ << indent() << "let message_ident = i_prot.read_message_begin()?;" << '\n';

  f_gen_ << indent() << "let res = match &*message_ident.name {"
         << '\n'; // [sigh] explicit deref coercion
  indent_up();
  render_process_match_statements(tservice);
  f_gen_ << indent() << "method => {" << '\n';
  indent_up();
  render_thrift_error("Application", "ApplicationError", "ApplicationErrorKind::UnknownMethod",
                      "format!(\"unknown method {}\", method)");
  indent_down();
  f_gen_ << indent() << "}," << '\n';

  indent_down();
  f_gen_ << indent() << "};" << '\n';
  f_gen_ << indent() << "thrift::server::handle_process_result(&message_ident, res, o_prot)"
         << '\n';

  indent_down();
  f_gen_ << indent() << "}" << '\n';

  indent_down();
  f_gen_ << indent() << "}" << '\n';
  f_gen_ << '\n';
}

void t_rs_generator::render_sync_process_delegation_functions(t_service* tservice) {
  string actual_processor(rust_namespace(tservice) + rust_sync_processor_impl_name(tservice));

  vector<t_function*> functions = tservice->get_functions();
  vector<t_function*>::iterator func_iter;
  for (func_iter = functions.begin(); func_iter != functions.end(); ++func_iter) {
    t_function* tfunc = (*func_iter);
    string function_name("process_" + rust_snake_case(tfunc->get_name()));
    f_gen_ << indent() << "fn " << function_name << "(&self, "
           << "incoming_sequence_number: i32, "
           << "i_prot: &mut dyn TInputProtocol, "
           << "o_prot: &mut dyn TOutputProtocol) "
           << "-> thrift::Result<()> {" << '\n';
    indent_up();

    f_gen_ << indent() << actual_processor << "::" << function_name << "("
           << "&self.handler, "
           << "incoming_sequence_number, "
           << "i_prot, "
           << "o_prot"
           << ")" << '\n';

    indent_down();
    f_gen_ << indent() << "}" << '\n';
  }

  t_service* extends = tservice->get_extends();
  if (extends) {
    render_sync_process_delegation_functions(extends);
  }
}

void t_rs_generator::render_process_match_statements(t_service* tservice) {
  vector<t_function*> functions = tservice->get_functions();
  vector<t_function*>::iterator func_iter;
  for (func_iter = functions.begin(); func_iter != functions.end(); ++func_iter) {
    t_function* tfunc = (*func_iter);
    f_gen_ << indent() << "\"" << tfunc->get_name() << "\""
           << " => {" << '\n'; // note: use *original* name
    indent_up();
    f_gen_ << indent() << "self.process_" << rust_snake_case(tfunc->get_name())
           << "(message_ident.sequence_number, i_prot, o_prot)" << '\n';
    indent_down();
    f_gen_ << indent() << "}," << '\n';
  }

  t_service* extends = tservice->get_extends();
  if (extends) {
    render_process_match_statements(extends);
  }
}

void t_rs_generator::render_sync_process_function(t_function* tfunc, const string& handler_type) {
  string sequence_number_param("incoming_sequence_number");
  string output_protocol_param("o_prot");

  if (tfunc->is_oneway()) {
    sequence_number_param = "_";
    output_protocol_param = "_";
  }

  f_gen_ << indent() << "pub fn process_" << rust_snake_case(tfunc->get_name())
         << "<H: " << handler_type << ">"
         << "(handler: &H, " << sequence_number_param << ": i32, "
         << "i_prot: &mut dyn TInputProtocol, " << output_protocol_param
         << ": &mut dyn TOutputProtocol) "
         << "-> thrift::Result<()> {" << '\n';

  indent_up();

  // *always* read arguments from the input protocol
  f_gen_ << indent() << "let " << (has_non_void_args(tfunc) ? "args" : "_") << " = "
         << service_call_args_struct_name(tfunc) << "::read_from_in_protocol(i_prot)?;" << '\n';

  f_gen_ << indent() << "match handler." << service_call_handler_function_name(tfunc)
         << rust_sync_service_call_invocation(tfunc, "args.") << " {" << '\n'; // start match
  indent_up();

  // handler succeeded
  string handler_return_variable
      = tfunc->is_oneway() || tfunc->get_returntype()->is_void() ? "_" : "handler_return";
  f_gen_ << indent() << "Ok(" << handler_return_variable << ") => {" << '\n';
  indent_up();
  render_sync_handler_succeeded(tfunc);
  indent_down();
  f_gen_ << indent() << "}," << '\n';
  // handler failed
  f_gen_ << indent() << "Err(e) => {" << '\n';
  indent_up();
  render_sync_handler_failed(tfunc);
  indent_down();
  f_gen_ << indent() << "}," << '\n';

  indent_down();
  f_gen_ << indent() << "}" << '\n'; // end match

  indent_down();
  f_gen_ << indent() << "}" << '\n'; // end function
}

void t_rs_generator::render_sync_handler_succeeded(t_function* tfunc) {
  if (tfunc->is_oneway()) {
    f_gen_ << indent() << "Ok(())" << '\n';
  } else {
    f_gen_ << indent() << "let message_ident = TMessageIdentifier::new("
           << "\"" << tfunc->get_name() << "\", " // note: use *original* name
           << "TMessageType::Reply, "
           << "incoming_sequence_number);" << '\n';
    f_gen_ << indent() << "o_prot.write_message_begin(&message_ident)?;" << '\n';
    f_gen_ << indent() << "let ret = " << handler_successful_return_struct(tfunc) << ";" << '\n';
    f_gen_ << indent() << "ret.write_to_out_protocol(o_prot)?;" << '\n';
    f_gen_ << indent() << "o_prot.write_message_end()?;" << '\n';
    f_gen_ << indent() << "o_prot.flush()" << '\n';
  }
}

void t_rs_generator::render_sync_handler_failed(t_function* tfunc) {
  string err_var("e");

  f_gen_ << indent() << "match " << err_var << " {" << '\n';
  indent_up();

  // if there are any user-defined exceptions for this service call handle them first
  if (tfunc->get_xceptions() != nullptr
      && tfunc->get_xceptions()->get_sorted_members().size() > 0) {
    string user_err_var("usr_err");
    f_gen_ << indent() << "thrift::Error::User(" << user_err_var << ") => {" << '\n';
    indent_up();
    render_sync_handler_failed_user_exception_branch(tfunc);
    indent_down();
    f_gen_ << indent() << "}," << '\n';
  }

  // application error
  string app_err_var("app_err");
  f_gen_ << indent() << "thrift::Error::Application(" << app_err_var << ") => {" << '\n';
  indent_up();
  render_sync_handler_failed_application_exception_branch(tfunc, app_err_var);
  indent_down();
  f_gen_ << indent() << "}," << '\n';

  // default case
  f_gen_ << indent() << "_ => {" << '\n';
  indent_up();
  render_sync_handler_failed_default_exception_branch(tfunc);
  indent_down();
  f_gen_ << indent() << "}," << '\n';

  indent_down();
  f_gen_ << indent() << "}" << '\n';
}

void t_rs_generator::render_sync_handler_failed_user_exception_branch(t_function* tfunc) {
  if (tfunc->get_xceptions() == nullptr || tfunc->get_xceptions()->get_sorted_members().empty()) {
    throw "cannot render user exception branches if no user exceptions defined";
  }

  const vector<t_field*> txceptions = tfunc->get_xceptions()->get_sorted_members();
  vector<t_field*>::const_iterator xception_iter;
  int branches_rendered = 0;

  // run through all user-defined exceptions
  for (xception_iter = txceptions.begin(); xception_iter != txceptions.end(); ++xception_iter) {
    t_field* xception_field = (*xception_iter);

    string if_statement(branches_rendered == 0 ? "if usr_err" : "} else if usr_err");
    string exception_type(to_rust_type(xception_field->get_type()));
    f_gen_ << indent() << if_statement << ".downcast_ref::<" << exception_type << ">().is_some() {"
           << '\n';
    indent_up();

    f_gen_ << indent() << "let err = usr_err.downcast::<" << exception_type
           << ">().expect(\"downcast already checked\");" << '\n';

    // render the members of the return struct
    ostringstream members;

    bool has_result_variable = !(tfunc->is_oneway() || tfunc->get_returntype()->is_void());
    if (has_result_variable) {
      members << SERVICE_RESULT_VARIABLE << ": None, ";
    }

    vector<t_field*>::const_iterator xception_members_iter;
    for (xception_members_iter = txceptions.begin(); xception_members_iter != txceptions.end();
         ++xception_members_iter) {
      t_field* member = (*xception_members_iter);
      string member_name(rust_field_name(member));
      if (member == xception_field) {
        members << member_name << ": Some(*err), ";
      } else {
        members << member_name << ": None, ";
      }
    }

    string member_string = members.str();
    member_string.replace(member_string.size() - 2, 2, " "); // trim trailing comma

    // now write out the return struct
    f_gen_ << indent() << "let ret_err = " << service_call_result_struct_name(tfunc) << "{ "
           << member_string << "};" << '\n';

    f_gen_ << indent() << "let message_ident = "
           << "TMessageIdentifier::new("
           << "\"" << tfunc->get_name() << "\", " // note: use *original* name
           << "TMessageType::Reply, "
           << "incoming_sequence_number);" << '\n';
    f_gen_ << indent() << "o_prot.write_message_begin(&message_ident)?;" << '\n';
    f_gen_ << indent() << "ret_err.write_to_out_protocol(o_prot)?;" << '\n';
    f_gen_ << indent() << "o_prot.write_message_end()?;" << '\n';
    f_gen_ << indent() << "o_prot.flush()" << '\n';

    indent_down();

    branches_rendered++;
  }

  // the catch all, if somehow it was a user exception that we don't support
  f_gen_ << indent() << "} else {" << '\n';
  indent_up();

  // FIXME: same as default block below

  f_gen_ << indent() << "let ret_err = {" << '\n';
  indent_up();
  render_thrift_error_struct("ApplicationError", "ApplicationErrorKind::Unknown",
                             "usr_err.to_string()");
  indent_down();
  f_gen_ << indent() << "};" << '\n';
  render_sync_handler_send_exception_response(tfunc, "ret_err");

  indent_down();
  f_gen_ << indent() << "}" << '\n';
}

void t_rs_generator::render_sync_handler_failed_application_exception_branch(
    t_function* tfunc,
    const string& app_err_var) {
  if (tfunc->is_oneway()) {
    f_gen_ << indent() << "Err(thrift::Error::Application(" << app_err_var << "))" << '\n';
  } else {
    render_sync_handler_send_exception_response(tfunc, app_err_var);
  }
}

void t_rs_generator::render_sync_handler_failed_default_exception_branch(t_function* tfunc) {
  f_gen_ << indent() << "let ret_err = {" << '\n';
  indent_up();
  render_thrift_error_struct("ApplicationError", "ApplicationErrorKind::Unknown", "e.to_string()");
  indent_down();
  f_gen_ << indent() << "};" << '\n';
  if (tfunc->is_oneway()) {
    f_gen_ << indent() << "Err(thrift::Error::Application(ret_err))" << '\n';
  } else {
    render_sync_handler_send_exception_response(tfunc, "ret_err");
  }
}

void t_rs_generator::render_sync_handler_send_exception_response(t_function* tfunc,
                                                                 const string& err_var) {
  f_gen_ << indent() << "let message_ident = TMessageIdentifier::new("
         << "\"" << tfunc->get_name() << "\", " // note: use *original* name
         << "TMessageType::Exception, "
         << "incoming_sequence_number);" << '\n';
  f_gen_ << indent() << "o_prot.write_message_begin(&message_ident)?;" << '\n';
  f_gen_ << indent() << "thrift::Error::write_application_error_to_out_protocol(&" << err_var
         << ", o_prot)?;" << '\n';
  f_gen_ << indent() << "o_prot.write_message_end()?;" << '\n';
  f_gen_ << indent() << "o_prot.flush()" << '\n';
}

string t_rs_generator::handler_successful_return_struct(t_function* tfunc) {
  int member_count = 0;
  ostringstream return_struct;

  return_struct << service_call_result_struct_name(tfunc) << " { ";

  // actual return
  if (!tfunc->get_returntype()->is_void()) {
    return_struct << "result_value: Some(handler_return)";
    member_count++;
  }

  // any user-defined exceptions
  if (tfunc->get_xceptions() != nullptr) {
    t_struct* txceptions = tfunc->get_xceptions();
    const vector<t_field*> members = txceptions->get_sorted_members();
    vector<t_field*>::const_iterator members_iter;
    for (members_iter = members.begin(); members_iter != members.end(); ++members_iter) {
      t_field* xception_field = (*members_iter);
      if (member_count > 0) {
        return_struct << ", ";
      }
      return_struct << rust_field_name(xception_field) << ": None";
      member_count++;
    }
  }

  return_struct << " }";

  return return_struct.str();
}

//-----------------------------------------------------------------------------
//
// Utility
//
//-----------------------------------------------------------------------------

void t_rs_generator::render_type_comment(const string& type_name) {
  f_gen_ << "//" << '\n';
  f_gen_ << "// " << type_name << '\n';
  f_gen_ << "//" << '\n';
  f_gen_ << '\n';
}

// NOTE: do *not* put in an extra newline after doc is generated.
// This is because rust docs have to abut the line they're documenting.
void t_rs_generator::render_rustdoc(t_doc* tdoc) {
  if (!tdoc->has_doc()) {
    return;
  }

  generate_docstring_comment(f_gen_, "", "/// ", tdoc->get_doc(), "");
}

void t_rs_generator::render_thrift_error(const string& error_kind,
                                         const string& error_struct,
                                         const string& sub_error_kind,
                                         const string& error_message) {
  f_gen_ << indent() << "Err(" << '\n';
  indent_up();
  f_gen_ << indent() << "thrift::Error::" << error_kind << "(" << '\n';
  indent_up();
  render_thrift_error_struct(error_struct, sub_error_kind, error_message);
  indent_down();
  f_gen_ << indent() << ")" << '\n';
  indent_down();
  f_gen_ << indent() << ")" << '\n';
}

void t_rs_generator::render_thrift_error_struct(const string& error_struct,
                                                const string& sub_error_kind,
                                                const string& error_message) {
  f_gen_ << indent() << error_struct << "::new(" << '\n';
  indent_up();
  f_gen_ << indent() << sub_error_kind << "," << '\n';
  f_gen_ << indent() << error_message << '\n';
  indent_down();
  f_gen_ << indent() << ")" << '\n';
}

bool t_rs_generator::is_double(t_type* ttype) {
  ttype = get_true_type(ttype);
  if (ttype->is_base_type()) {
    t_base_type::t_base tbase = ((t_base_type*)ttype)->get_base();
    if (tbase == t_base_type::TYPE_DOUBLE) {
      return true;
    }
  }

  return false;
}

string t_rs_generator::to_rust_type(t_type* ttype) {
  // ttype = get_true_type(ttype); <-- recurses through as many typedef layers as necessary
  if (ttype->is_base_type()) {
    t_base_type* tbase_type = ((t_base_type*)ttype);
    switch (tbase_type->get_base()) {
    case t_base_type::TYPE_VOID:
      return "()";
    case t_base_type::TYPE_STRING:
      if (tbase_type->is_binary()) {
        return "Vec<u8>";
      } else {
        return "String";
      }
    case t_base_type::TYPE_UUID:
      return "uuid::Uuid";
    case t_base_type::TYPE_BOOL:
      return "bool";
    case t_base_type::TYPE_I8:
      return "i8";
    case t_base_type::TYPE_I16:
      return "i16";
    case t_base_type::TYPE_I32:
      return "i32";
    case t_base_type::TYPE_I64:
      return "i64";
    case t_base_type::TYPE_DOUBLE:
      return "OrderedFloat<f64>";
    default:
      throw "compiler error: unhandled type";
    }
  } else if (ttype->is_typedef()) {
    t_typedef* ttypedef = (t_typedef*)ttype;
    string rust_type = rust_namespace(ttype) + ttypedef->get_symbolic();
    rust_type = ttypedef->is_forward_typedef() ? "Box<" + rust_type + ">" : rust_type;
    return rust_type;
  } else if (ttype->is_enum()) {
    return rust_namespace(ttype) + rust_camel_case(ttype->get_name());
  } else if (ttype->is_struct() || ttype->is_xception()) {
    return rust_namespace(ttype) + rust_camel_case(ttype->get_name());
  } else if (ttype->is_map()) {
    t_map* tmap = (t_map*)ttype;
    return "BTreeMap<" + to_rust_type(tmap->get_key_type()) + ", "
           + to_rust_type(tmap->get_val_type()) + ">";
  } else if (ttype->is_set()) {
    t_set* tset = (t_set*)ttype;
    return "BTreeSet<" + to_rust_type(tset->get_elem_type()) + ">";
  } else if (ttype->is_list()) {
    t_list* tlist = (t_list*)ttype;
    return "Vec<" + to_rust_type(tlist->get_elem_type()) + ">";
  }

  throw "cannot find rust type for " + ttype->get_name();
}

string t_rs_generator::to_rust_const_type(t_type* ttype) {
  if (ttype->is_base_type()) {
    t_base_type* tbase_type = ((t_base_type*)ttype);
    if (tbase_type->get_base() == t_base_type::TYPE_STRING) {
      if (tbase_type->is_binary()) {
        return "&[u8]";
      } else {
        return "&str";
      }
    }
  }

  return to_rust_type(ttype);
}

string t_rs_generator::to_rust_field_type_enum(t_type* ttype) {
  ttype = get_true_type(ttype);
  if (ttype->is_base_type()) {
    t_base_type::t_base tbase = ((t_base_type*)ttype)->get_base();
    switch (tbase) {
    case t_base_type::TYPE_VOID:
      throw "will not generate protocol::TType for TYPE_VOID";
    case t_base_type::TYPE_STRING: // both strings and binary are actually encoded as TType::String
      return "TType::String";
    case t_base_type::TYPE_UUID:
      return "TType::Uuid";
    case t_base_type::TYPE_BOOL:
      return "TType::Bool";
    case t_base_type::TYPE_I8:
      return "TType::I08";
    case t_base_type::TYPE_I16:
      return "TType::I16";
    case t_base_type::TYPE_I32:
      return "TType::I32";
    case t_base_type::TYPE_I64:
      return "TType::I64";
    case t_base_type::TYPE_DOUBLE:
      return "TType::Double";
    default:
      throw "compiler error: unhandled type";
    }
  } else if (ttype->is_enum()) {
    return "TType::I32";
  } else if (ttype->is_struct() || ttype->is_xception()) {
    return "TType::Struct";
  } else if (ttype->is_map()) {
    return "TType::Map";
  } else if (ttype->is_set()) {
    return "TType::Set";
  } else if (ttype->is_list()) {
    return "TType::List";
  }

  throw "cannot find TType for " + ttype->get_name();
}

string t_rs_generator::opt_in_req_out_value(t_type* ttype) {
  ttype = get_true_type(ttype);
  if (ttype->is_base_type()) {
    t_base_type* tbase_type = ((t_base_type*)ttype);
    switch (tbase_type->get_base()) {
    case t_base_type::TYPE_VOID:
      throw "cannot generate OPT_IN_REQ_OUT value for void";
    case t_base_type::TYPE_STRING:
      if (tbase_type->is_binary()) {
        return "Some(Vec::new())";
      } else {
        return "Some(\"\".to_owned())";
      }
    case t_base_type::TYPE_UUID:
      return "Some(uuid::Uuid::nil())";
    case t_base_type::TYPE_BOOL:
      return "Some(false)";
    case t_base_type::TYPE_I8:
    case t_base_type::TYPE_I16:
    case t_base_type::TYPE_I32:
    case t_base_type::TYPE_I64:
      return "Some(0)";
    case t_base_type::TYPE_DOUBLE:
      return "Some(OrderedFloat::from(0.0))";
    default:
      throw "compiler error: unhandled type";
    }

  } else if (ttype->is_enum() || ttype->is_struct() || ttype->is_xception()) {
    return "None";
  } else if (ttype->is_list()) {
    return "Some(Vec::new())";
  } else if (ttype->is_set()) {
    return "Some(BTreeSet::new())";
  } else if (ttype->is_map()) {
    return "Some(BTreeMap::new())";
  }

  throw "cannot generate opt-in-req-out value for type " + ttype->get_name();
}

bool t_rs_generator::can_generate_simple_const(t_type* ttype) {
  t_type* actual_type = get_true_type(ttype);
  if (actual_type->is_base_type()) {
    t_base_type* tbase_type = (t_base_type*)actual_type;
    return !(tbase_type->get_base() == t_base_type::TYPE_DOUBLE);
  } else {
    return false;
  }
}

bool t_rs_generator::can_generate_const_holder(t_type* ttype) {
  t_type* actual_type = get_true_type(ttype);
  return !can_generate_simple_const(actual_type) && !actual_type->is_service();
}

bool t_rs_generator::is_void(t_type* ttype) {
  return ttype->is_base_type() && ((t_base_type*)ttype)->get_base() == t_base_type::TYPE_VOID;
}

bool t_rs_generator::is_optional(t_field::e_req req) {
  return req == t_field::T_OPTIONAL || req == t_field::T_OPT_IN_REQ_OUT;
}

t_field::e_req t_rs_generator::actual_field_req(t_field* tfield,
                                                t_rs_generator::e_struct_type struct_type) {
  return struct_type == t_rs_generator::T_ARGS ? t_field::T_REQUIRED : tfield->get_req();
}

bool t_rs_generator::has_args(t_function* tfunc) {
  return tfunc->get_arglist() != nullptr && !tfunc->get_arglist()->get_sorted_members().empty();
}

bool t_rs_generator::has_non_void_args(t_function* tfunc) {
  bool has_non_void_args = false;

  const vector<t_field*> args = tfunc->get_arglist()->get_sorted_members();
  vector<t_field*>::const_iterator args_iter;
  for (args_iter = args.begin(); args_iter != args.end(); ++args_iter) {
    t_field* tfield = (*args_iter);
    if (!tfield->get_type()->is_void()) {
      has_non_void_args = true;
      break;
    }
  }

  return has_non_void_args;
}

string t_rs_generator::visibility_qualifier(t_rs_generator::e_struct_type struct_type) {
  switch (struct_type) {
  case t_rs_generator::T_ARGS:
  case t_rs_generator::T_RESULT:
    return "";
  default:
    return "pub ";
  }
}

string t_rs_generator::rust_namespace(t_service* tservice) {
  if (tservice->get_program()->get_name() != get_program()->get_name()) {
    return rust_snake_case(tservice->get_program()->get_name()) + "::";
  } else {
    return "";
  }
}

string t_rs_generator::rust_namespace(t_type* ttype) {
  if (ttype->get_program()->get_name() != get_program()->get_name()) {
    return rust_snake_case(ttype->get_program()->get_name()) + "::";
  } else {
    return "";
  }
}

bool t_rs_generator::is_reserved(const string& name) {
  return RUST_RESERVED_WORDS_SET.find(name) != RUST_RESERVED_WORDS_SET.end();
}

string t_rs_generator::rust_struct_name(t_struct* tstruct) {
  string base_struct_name(rust_camel_case(tstruct->get_name()));
  return rust_safe_name(base_struct_name);
}

string t_rs_generator::rust_field_name(t_field* tfield) {
  string base_field_name(rust_snake_case(tfield->get_name()));
  return rust_safe_name(base_field_name);
}

string t_rs_generator::rust_union_field_name(t_field* tfield) {
  string base_field_name(rust_camel_case(tfield->get_name()));
  return rust_safe_name(base_field_name);
}

string t_rs_generator::rust_safe_name(const string& name) {
  if (is_reserved(name)) {
    return name + "_";
  } else {
    return name;
  }
}

string t_rs_generator::service_call_client_function_name(t_function* tfunc) {
  return rust_snake_case(tfunc->get_name());
}

string t_rs_generator::service_call_handler_function_name(t_function* tfunc) {
  return "handle_" + rust_snake_case(tfunc->get_name());
}

string t_rs_generator::service_call_args_struct_name(t_function* tfunc) {
  // Thrift automatically appends `Args` to the arglist name. No need to do it here.
  return rust_camel_case(service_name_) + rust_camel_case(tfunc->get_arglist()->get_name());
}

string t_rs_generator::service_call_result_struct_name(t_function* tfunc) {
  return rust_camel_case(service_name_) + rust_camel_case(tfunc->get_name()) + RESULT_STRUCT_SUFFIX;
}

string t_rs_generator::rust_sync_client_marker_trait_name(t_service* tservice) {
  return "T" + rust_camel_case(tservice->get_name()) + "SyncClientMarker";
}

string t_rs_generator::rust_sync_client_trait_name(t_service* tservice) {
  return "T" + rust_camel_case(tservice->get_name()) + "SyncClient";
}

string t_rs_generator::rust_sync_client_impl_name(t_service* tservice) {
  return rust_camel_case(tservice->get_name()) + "SyncClient";
}

string t_rs_generator::rust_sync_handler_trait_name(t_service* tservice) {
  return rust_camel_case(tservice->get_name()) + "SyncHandler";
}

string t_rs_generator::rust_sync_processor_name(t_service* tservice) {
  return rust_camel_case(tservice->get_name()) + "SyncProcessor";
}

string t_rs_generator::rust_sync_processor_impl_name(t_service* tservice) {
  return "T" + rust_camel_case(tservice->get_name()) + "ProcessFunctions";
}

string t_rs_generator::rust_enum_variant_name(const string& name) {
  bool all_uppercase = true;

  for (char i : name) {
    if (isalpha(i) && islower(i)) {
      all_uppercase = false;
      break;
    }
  }

  if (all_uppercase) {
    return name;
  } else {
    string modified_name(uppercase(underscore(name)));
    string_replace(modified_name, "__", "_");
    return modified_name;
  }
}

string t_rs_generator::rust_upper_case(const string& name) {
  bool all_uppercase = true;

  for (char i : name) {
    if (isalpha(i) && islower(i)) {
      all_uppercase = false;
      break;
    }
  }

  if (all_uppercase) {
    return name;
  } else {
    string str(uppercase(underscore(name)));
    string_replace(str, "__", "_");
    return str;
  }
}

string t_rs_generator::rust_snake_case(const string& name) {
  string str(decapitalize(underscore(name)));
  string_replace(str, "__", "_");
  return str;
}

string t_rs_generator::rust_camel_case(const string& name) {
  string str(capitalize(camelcase(name)));
  string_replace(str, "_", "");
  return str;
}

string t_rs_generator::rust_safe_field_id(int32_t id) {
  string id_str = std::to_string(abs(id));
  if (id >= 0) {
    return id_str;
  } else {
    string str("neg");
    str += id_str;
    return str;
  }
}

void t_rs_generator::string_replace(string& target,
                                    const string& search_string,
                                    const string& replace_string) {
  if (target.empty()) {
    return;
  }

  size_t match_len = search_string.length();
  size_t replace_len = replace_string.length();

  size_t search_idx = 0;
  size_t match_idx;
  while ((match_idx = target.find(search_string, search_idx)) != string::npos) {
    target.replace(match_idx, match_len, replace_string);
    search_idx = match_idx + replace_len;
  }
}

std::string t_rs_generator::display_name() const {
  return "Rust";
}

THRIFT_REGISTER_GENERATOR(rs, "Rust", "\n") // no Rust-generator-specific options
