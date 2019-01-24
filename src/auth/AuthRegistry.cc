// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "AuthRegistry.h"

#include "cephx/CephxAuthorizeHandler.h"
#ifdef HAVE_GSSAPI
#include "krb/KrbAuthorizeHandler.hpp"
#endif
#include "none/AuthNoneAuthorizeHandler.h"
#include "common/ceph_context.h"
#include "common/debug.h"
#include "auth/KeyRing.h"

#define dout_subsys ceph_subsys_auth
#undef dout_prefix
#define dout_prefix *_dout << "AuthRegistry(" << this << ") "

const char** AuthRegistry::get_tracked_conf_keys() const
{
  static const char *keys[] = {
    "auth_supported",
    "auth_client_required",
    "auth_cluster_required",
    "auth_service_required",
    "keyring",
    NULL
  };
  return keys;
}

void AuthRegistry::handle_conf_change(
  const ConfigProxy& conf,
  const std::set<std::string>& changed)
{
  std::scoped_lock l(lock);
  _refresh_config();
}


void AuthRegistry::_parse_method_list(const string& s,
				      std::vector<uint32_t> *v)
{
  std::list<std::string> sup_list;
  get_str_list(s, sup_list);
  if (sup_list.empty()) {
    lderr(cct) << "WARNING: empty auth protocol list" << dendl;
  }
  for (auto& i : sup_list) {
    ldout(cct, 5) << "adding auth protocol: " << i << dendl;
    if (i == "cephx") {
      v->push_back(CEPH_AUTH_CEPHX);
    } else if (i == "none") {
      v->push_back(CEPH_AUTH_NONE);
    } else if (i == "gss") {
      v->push_back(CEPH_AUTH_GSS);
    } else {
      v->push_back(CEPH_AUTH_UNKNOWN);
      lderr(cct) << "WARNING: unknown auth protocol defined: " << i << dendl;
    }
  }
  if (v->empty()) {
    lderr(cct) << "WARNING: no auth protocol defined, use 'cephx' by default"
	       << dendl;
    v->push_back(CEPH_AUTH_CEPHX);
  }
  ldout(cct,20) << __func__ << " " << s << " -> " << *v << dendl;
}

void AuthRegistry::_refresh_config()
{
  if (cct->_conf->auth_supported.size()) {
    _parse_method_list(cct->_conf->auth_supported, &cluster_methods);
    _parse_method_list(cct->_conf->auth_supported, &service_methods);
    _parse_method_list(cct->_conf->auth_supported, &client_methods);
  } else {
    _parse_method_list(cct->_conf->auth_cluster_required, &cluster_methods);
    _parse_method_list(cct->_conf->auth_service_required, &service_methods);
    _parse_method_list(cct->_conf->auth_client_required, &client_methods);
  }

  ldout(cct,10) << __func__ << " cluster_methods " << cluster_methods
		<< " service_methods " << service_methods
		<< " client_methods " << client_methods
		<< dendl;

  // if we have no keyring, filter out cephx
  _no_keyring_disabled_cephx = false;
  KeyRing k;
  int r = k.from_ceph_context(cct);
  if (r == -ENOENT) {
    for (auto *p : {&cluster_methods, &service_methods, &client_methods}) {
      auto q = std::find(p->begin(), p->end(), CEPH_AUTH_CEPHX);
      if (q != p->end()) {
	p->erase(q);
	_no_keyring_disabled_cephx = true;
      }
    }
  }
  if (_no_keyring_disabled_cephx) {
    lderr(cct) << "no keyring found at " << cct->_conf->keyring
	       << ", disabling cephx" << dendl;
  }
}

void AuthRegistry::get_supported_methods(int peer_type,
					 std::vector<uint32_t> *v)
{
  if (cct->get_module_type() == CEPH_ENTITY_TYPE_CLIENT) {
    *v = client_methods;
    return;
  }
  switch (peer_type) {
  case CEPH_ENTITY_TYPE_MON:
  case CEPH_ENTITY_TYPE_MGR:
  case CEPH_ENTITY_TYPE_MDS:
  case CEPH_ENTITY_TYPE_OSD:
    *v = cluster_methods;
    break;
  default:
    *v = service_methods;
    break;
  }
}

bool AuthRegistry::is_supported_method(int peer_type, int method)
{
  std::vector<uint32_t> s;
  get_supported_methods(peer_type, &s);
  return std::find(s.begin(), s.end(), method) != s.end();
}

bool AuthRegistry::any_supported_methods(int peer_type)
{
  std::vector<uint32_t> s;
  get_supported_methods(peer_type, &s);
  return !s.empty();
}

AuthAuthorizeHandler *AuthRegistry::get_handler(int peer_type, int method)
{
  std::scoped_lock l{lock};
  ldout(cct,20) << __func__ << " peer_type " << peer_type << " method " << method
		<< " cluster_methods " << cluster_methods
		<< " service_methods " << service_methods
		<< " client_methods " << client_methods
		<< dendl;
  if (cct->get_module_type() == CEPH_ENTITY_TYPE_CLIENT) {
    return nullptr;
  }
  switch (peer_type) {
  case CEPH_ENTITY_TYPE_MON:
  case CEPH_ENTITY_TYPE_MGR:
  case CEPH_ENTITY_TYPE_MDS:
  case CEPH_ENTITY_TYPE_OSD:
    if (std::find(cluster_methods.begin(), cluster_methods.end(), method) ==
	cluster_methods.end()) {
      return nullptr;
    }
    break;
  default:
    if (std::find(service_methods.begin(), service_methods.end(), method) ==
	service_methods.end()) {
      return nullptr;
    }
    break;
  }

  auto iter = authorize_handlers.find(method);
  if (iter != authorize_handlers.end()) {
    return iter->second;
  }
  AuthAuthorizeHandler *ah = nullptr;
  switch (method) {
  case CEPH_AUTH_NONE:
    ah = new AuthNoneAuthorizeHandler();
    break;
  case CEPH_AUTH_CEPHX:
    ah = new CephxAuthorizeHandler();
    break;
#ifdef HAVE_GSSAPI
  case CEPH_AUTH_GSS:
    ah = new KrbAuthorizeHandler();
    break;
#endif
  }
  if (ah) {
    authorize_handlers[method] = ah;
  }
  return ah;
}

AuthRegistry::~AuthRegistry()
{
  for (auto i : authorize_handlers) {
    delete i.second;
  }
}
