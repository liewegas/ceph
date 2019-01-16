// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#pragma once

#include <vector>

class EntityName;
class AuthMethodList;
class CryptoKey;

class AuthClient {
public:
  virtual ~AuthClient() {}

  virtual int get_auth_request(
    Connection *con,
    uint32_t *method, bufferlist *out) = 0;
  virtual int handle_auth_reply_more(
    Connection *con,
    int result,
    const bufferlist& bl,
    bufferlist *reply) = 0;
  virtual void handle_auth_done(
    Connection *con,
    int result,
    uint64_t global_id,
    const bufferlist& bl,
    CryptoKey *session_key,
    CryptoKey *connection_key) = 0;
  virtual void handle_auth_bad_method(
    Connection *con,
    uint32_t old_auth_method,
    const std::vector<uint32_t>& allowed_methods) = 0;
};
