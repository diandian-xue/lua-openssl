/*=========================================================================*\
* x509.c
* x509 modules for lua-openssl binding
*
* Author:  george zhao <zhaozg(at)gmail.com>
\*=========================================================================*/
#include "openssl.h"
#include "private.h"
#define CRYPTO_LOCK_REF
#include "sk.h"

#define MYNAME    "x509"
#define MYVERSION MYNAME " library for " LUA_VERSION " / Nov 2014 / "\
  "based on OpenSSL " SHLIB_VERSION_NUMBER

int openssl_push_x509_algor(lua_State*L, const X509_ALGOR* alg) {
  lua_newtable(L);
  openssl_push_asn1object(L, alg->algorithm);
  lua_setfield(L, -2, "algorithm");
  if (alg->parameter) {
    openssl_push_asn1type(L, alg->parameter);
    lua_setfield(L, -2, "parameter");
  }
  return 1;
};

int opensl_push_general_name(lua_State*L, const GENERAL_NAME* general_name, int utf8) {
  lua_newtable(L);

  switch (general_name->type) {
  case GEN_OTHERNAME:
    {
      OTHERNAME *otherName = general_name->d.otherName;
      lua_newtable(L);
      openssl_push_asn1object(L, otherName->type_id);
      PUSH_ASN1_STRING(L, otherName->value->value.asn1_string, utf8);
      lua_settable(L, -3);
      lua_setfield(L, -2, "otherName");

      lua_pushstring(L, "otherName");
      lua_setfield(L, -2, "type");
      break;
    }
  case GEN_EMAIL:
    PUSH_ASN1_STRING(L, general_name->d.rfc822Name, utf8);
    lua_setfield(L, -2, "rfc822Name");

    lua_pushstring(L, "rfc822Name");
    lua_setfield(L, -2, "type");
    break;
  case GEN_DNS:
    PUSH_ASN1_STRING(L, general_name->d.dNSName, utf8);
    lua_setfield(L, -2, "dNSName");
    lua_pushstring(L, "dNSName");
    lua_setfield(L, -2, "type");
    break;
  case GEN_X400:
    openssl_push_asn1type(L, general_name->d.x400Address);
    lua_setfield(L, -2, "x400Address");
    lua_pushstring(L, "x400Address");
    lua_setfield(L, -2, "type");
    break;
  case GEN_DIRNAME:
    {
      X509_NAME* xn = general_name->d.directoryName;
      openssl_push_xname_asobject(L, xn);
      lua_setfield(L, -2, "directoryName");
      lua_pushstring(L, "directoryName");
      lua_setfield(L, -2, "type");
    }
    break;
  case GEN_URI:
    PUSH_ASN1_STRING(L, general_name->d.uniformResourceIdentifier, utf8);
    lua_setfield(L, -2, "uniformResourceIdentifier");
    lua_pushstring(L, "uniformResourceIdentifier");
    lua_setfield(L, -2, "type");
    break;
  case GEN_IPADD:
    lua_newtable(L);
    PUSH_ASN1_OCTET_STRING(L, general_name->d.iPAddress);
    lua_setfield(L, -2, "iPAddress");
    lua_pushstring(L, "iPAddress");
    lua_setfield(L, -2, "type");
    break;
  case GEN_EDIPARTY:
    lua_newtable(L);
    PUSH_ASN1_STRING(L, general_name->d.ediPartyName->nameAssigner,utf8);
    lua_setfield(L, -2, "nameAssigner");
    PUSH_ASN1_STRING(L, general_name->d.ediPartyName->partyName,utf8);
    lua_setfield(L, -2, "partyName");
    lua_setfield(L, -2, "ediPartyName");

    lua_pushstring(L, "ediPartyName");
    lua_setfield(L, -2, "type");
    break;
  case GEN_RID:
    lua_newtable(L);
    openssl_push_asn1object(L, general_name->d.registeredID);
    lua_setfield(L, -2, "registeredID");
    lua_pushstring(L, "registeredID");
    lua_setfield(L, -2, "type");
    break;
  default:
    lua_pushstring(L, "unsupport");
    lua_setfield(L, -2, "type");
  }
  return 1;
};

/*** openssl.x509 object methods ***/
X509_STORE * skX509_to_store(STACK_OF(X509)* calist, const char* files, const char* dirs)
{
  X509_STORE *store = X509_STORE_new();
  if (store)
  {
    int i;
    for (i = 0; i < sk_X509_num(calist); i++)
    {
      X509 *x = sk_X509_value(calist, i);
      X509_STORE_add_cert(store, x);
    }

    if (files)
    {
      X509_LOOKUP *file_lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file());
      if (file_lookup)
      {
        X509_LOOKUP_load_file(file_lookup, files, X509_FILETYPE_DEFAULT);
      }
    }
    if (dirs)
    {
      X509_LOOKUP *dir_lookup = X509_STORE_add_lookup(store, X509_LOOKUP_hash_dir());
      if (dir_lookup)
      {
        X509_LOOKUP_add_dir(dir_lookup, dirs, X509_FILETYPE_DEFAULT);
      }
    }
  }
  return store;
}

static int check_cert(lua_State*L, X509_STORE *ca, X509 *x, STACK_OF(X509) *untrustedchain, int purpose)
{
  int ret = 0;
  X509_STORE_CTX *csc = X509_STORE_CTX_new();
  if (csc)
  {
    X509_STORE_set_flags(ca, X509_V_FLAG_CHECK_SS_SIGNATURE);
    if (X509_STORE_CTX_init(csc, ca, x, untrustedchain) == 1)
    {
      if (purpose > 0)
      {
        X509_STORE_CTX_set_purpose(csc, purpose);
      }
      ret = X509_verify_cert(csc);
    }
    X509_STORE_CTX_free(csc);
    return ret;
  }
  else
    luaL_error(L, "lua-openssl inner error");
  return 0;
}

static LUA_FUNCTION(openssl_x509_read)
{
  X509 *cert = NULL;

  BIO *in = load_bio_object(L, 1);
  int fmt = luaL_checkoption(L, 2, "auto", format);
  if (fmt == FORMAT_AUTO || fmt == FORMAT_DER)
  {
    cert = d2i_X509_bio(in, NULL);
    BIO_reset(in);
  }
  if ((fmt == FORMAT_AUTO && cert == NULL) || fmt == FORMAT_PEM)
  {
    cert = PEM_read_bio_X509(in, NULL, NULL, NULL);
    BIO_reset(in);
  }

  BIO_free(in);

  if (cert)
  {
    ERR_clear_error();
    PUSH_OBJECT(cert, "openssl.x509");
    return 1;
  }
  else
  {
    if (!lua_isnoneornil(L, 2))
      lua_pushfstring(L, "Invalid X509 certificate content with format %s", lua_tostring(L, 2));
    else
      lua_pushfstring(L, "Invalid X509 certificate content");
    luaL_argerror(L, 1, lua_tostring(L, -1));
  }
  return 0;
}

static LUA_FUNCTION(openssl_x509_export)
{
  X509 *cert = CHECK_OBJECT(1, X509, "openssl.x509");
  int fmt = luaL_checkoption(L, 2, "pem", format);
  int notext = lua_isnoneornil(L, 3) ? 1 : lua_toboolean(L, 3);
  BIO* out = NULL;

  if (fmt != FORMAT_DER && fmt != FORMAT_PEM)
  {
    luaL_argerror(L, 2, "format only accept pem or der");
  }

  out  = BIO_new(BIO_s_mem());
  if (fmt == FORMAT_PEM)
  {
    if (!notext)
    {
      X509_print(out, cert);
    }

    if (PEM_write_bio_X509(out, cert))
    {
      BUF_MEM *bio_buf;
      BIO_get_mem_ptr(out, &bio_buf);
      lua_pushlstring(L, bio_buf->data, bio_buf->length);
    }
    else
      lua_pushnil(L);
  }
  else
  {
    if (i2d_X509_bio(out, cert))
    {
      BUF_MEM *bio_buf;
      BIO_get_mem_ptr(out, &bio_buf);
      lua_pushlstring(L, bio_buf->data, bio_buf->length);
    }
    else
      lua_pushnil(L);
  }

  BIO_free(out);
  return 1;
};


static LUA_FUNCTION(openssl_x509_parse)
{
  int i;
  X509 * cert = CHECK_OBJECT(1, X509, "openssl.x509");
  int utf8 = lua_isnoneornil(L, 2) ? 1 : lua_toboolean(L, 2);
  int useshortnames = lua_isnoneornil(L, 3) ? 0 : lua_toboolean(L, 3);

  lua_newtable(L);

  if (cert->name)
  {
    AUXILIAR_SET(L, -1, "name", cert->name, string);
  }

  AUXILIAR_SET(L, -1, "valid", cert->valid, boolean);
  AUXILIAR_SET(L, -1, "version", X509_get_version(cert), integer);

  openssl_push_xname_asobject(L, X509_get_subject_name(cert));
  lua_setfield(L, -2, "subject");
  openssl_push_xname_asobject(L, X509_get_issuer_name(cert));
  lua_setfield(L, -2, "issuer");

  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%08lx", X509_subject_name_hash(cert));
    AUXILIAR_SET(L, -1, "hash", buf, string);
  }

  PUSH_ASN1_INTEGER(L, cert->cert_info->serialNumber);
  lua_setfield(L,-2, "serialNumber");
  PUSH_ASN1_TIME(L, X509_get_notBefore(cert));
  lua_setfield(L,-2, "notBefore");
  PUSH_ASN1_TIME(L, X509_get_notAfter(cert));
  lua_setfield(L,-2, "notAfter");

  {
    int l = 0;
    char* tmpstr = (char *)X509_alias_get0(cert, &l);
    if (tmpstr)
    {
      AUXILIAR_SETLSTR(L, -1, "alias", tmpstr, l);
    }
  }

  AUXILIAR_SET(L, -1, "ca", X509_check_ca(cert), boolean);

  lua_newtable(L);
  for (i = 0; i < X509_PURPOSE_get_count(); i++)
  {
    int set;
    X509_PURPOSE *purp = X509_PURPOSE_get0(i);
    int id = X509_PURPOSE_get_id(purp);
    const char * pname = useshortnames ? X509_PURPOSE_get0_sname(purp) : X509_PURPOSE_get0_name(purp);

    set = X509_check_purpose(cert, id, 0);
    if (set)
    {
      AUXILIAR_SET(L, -1, pname, 1, boolean);
    }
    set = X509_check_purpose(cert, id, 1);
    if (set)
    {
      lua_pushfstring(L, "%s CA", pname);
      pname = lua_tostring(L, -1);
      AUXILIAR_SET(L, -2, pname, 1, boolean);
      lua_pop(L, 1);
    }
  }
  lua_setfield(L, -2, "purposes");

  {
    int n = X509_get_ext_count(cert);
    if(n>0) {
      STACK_OF(X509_EXTENSION) *extensions = sk_X509_EXTENSION_new_null();
      int i;
      for(i=0; i<n; i++)
      {
        X509_EXTENSION *ext = X509_get_ext(cert, i);
        ext = X509_EXTENSION_dup(ext);
        sk_X509_EXTENSION_push(extensions, ext);
      }
      PUSH_OBJECT(extensions,"openssl.stack_of_x509_extension");
      lua_setfield(L,-2, "extensions");
    }
  }

  return 1;
}

static LUA_FUNCTION(openssl_x509_free)
{
  X509 *cert = CHECK_OBJECT(1, X509, "openssl.x509");
  lua_pushnil(L);
  lua_setmetatable(L, 1);
  X509_free(cert);
  return 0;
}

static LUA_FUNCTION(openssl_x509_public_key)
{
  X509 *cert = CHECK_OBJECT(1, X509, "openssl.x509");
  if (lua_isnone(L, 2)) {
    EVP_PKEY *pkey = X509_get_pubkey(cert);
    PUSH_OBJECT(pkey, "openssl.evp_pkey");
    return 1;
  }else{
    EVP_PKEY* pkey = CHECK_OBJECT(2,EVP_PKEY, "openssl.evp_pkey");
    int ret = X509_set_pubkey(cert, pkey);
    return openssl_pushresult(L, ret);
  }
}

const static int iPurpose[] =
{
  0,
  X509_PURPOSE_SSL_CLIENT,
  X509_PURPOSE_SSL_SERVER,
  X509_PURPOSE_NS_SSL_SERVER,
  X509_PURPOSE_SMIME_SIGN,
  X509_PURPOSE_SMIME_ENCRYPT,
  X509_PURPOSE_CRL_SIGN,
  X509_PURPOSE_ANY,
  X509_PURPOSE_OCSP_HELPER,
#if OPENSSL_VERSION_NUMBER > 0x10000000L
  X509_PURPOSE_TIMESTAMP_SIGN,
#endif
  0
};

const static char* sPurpose[] =
{
  "NONE",
  "ssl_client",
  "ssl_server",
  "ns_ssl_server",
  "smime_sign",
  "smime_encrypt",
  "crl_sign",
  "any",
  "ocsp_helper",
#if OPENSSL_VERSION_NUMBER > 0x10000000L
  "timestamp_sign",
#endif
  NULL
};

static int verify_cb(int ok, X509_STORE_CTX *ctx)
{
  char buf[256];

  if (!ok)
  {
    if (ctx->current_cert)
    {
      X509_NAME_oneline(
        X509_get_subject_name(ctx->current_cert), buf,
        sizeof buf);
      printf("%s\n", buf);
    }
    printf("error %d at %d depth lookup:%s\n", ctx->error,
           ctx->error_depth,
           X509_verify_cert_error_string(ctx->error));

    if (ctx->error == X509_V_ERR_CERT_HAS_EXPIRED) ok = 1;
    /* since we are just checking the certificates, it is
     * ok if they are self signed. But we should still warn
     * the user.
     */
    if (ctx->error == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT) ok = 1;

    /* Continue after extension errors too */
    if (ctx->error == X509_V_ERR_INVALID_CA) ok = 1;
    if (ctx->error == X509_V_ERR_INVALID_NON_CA) ok = 1;
    if (ctx->error == X509_V_ERR_PATH_LENGTH_EXCEEDED) ok = 1;
    if (ctx->error == X509_V_ERR_INVALID_PURPOSE) ok = 1;

    if (ctx->error == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT) ok = 1;
    if (ctx->error == X509_V_ERR_CRL_HAS_EXPIRED) ok = 1;
    if (ctx->error == X509_V_ERR_CRL_NOT_YET_VALID) ok = 1;
    if (ctx->error == X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION) ok = 1;
    /*
    if (ctx->error == X509_V_ERR_NO_EXPLICIT_POLICY)
      policies_print(NULL, ctx);
    */
    return ok;

  }
  /*
  if ((ctx->error == X509_V_OK) && (ok == 2))
    policies_print(NULL, ctx);
  */
  return (ok);
}


static LUA_FUNCTION(openssl_x509_check)
{
  X509 * cert = CHECK_OBJECT(1, X509, "openssl.x509");
  if (auxiliar_isclass(L, "openssl.evp_pkey", 2))
  {
    EVP_PKEY * key = CHECK_OBJECT(2, EVP_PKEY, "openssl.evp_pkey");
    lua_pushboolean(L, X509_check_private_key(cert, key));
  }
  else
  {
    STACK_OF(X509)* cert_stack =  CHECK_OBJECT(2, STACK_OF(X509), "openssl.stack_of_x509");
    STACK_OF(X509)* untrustedchain = lua_isnoneornil(L, 3) ?  NULL : CHECK_OBJECT(3, STACK_OF(X509), "openssl.stack_of_x509");
    int purpose = auxiliar_checkoption(L, 4, "NONE", sPurpose, iPurpose);

    X509_STORE * cainfo = skX509_to_store(cert_stack, NULL, NULL);
    int ret = 0;
    /*
    X509_STORE_set_verify_cb_func(cainfo,verify_cb);
    */
    ret = check_cert(L, cainfo, cert, untrustedchain, purpose);
    lua_pushboolean(L, ret);
    X509_STORE_free(cainfo);
  }

  return 1;
}

IMP_LUA_SK(X509, x509)

static STACK_OF(X509) * load_all_certs_from_file(BIO *in)
{
  STACK_OF(X509) *stack = sk_X509_new_null();
  if (stack)
  {
    STACK_OF(X509_INFO) *sk = PEM_X509_INFO_read_bio(in, NULL, NULL, NULL);
    /* scan over it and pull out the certs */
    while (sk_X509_INFO_num(sk))
    {
      X509_INFO *xi = sk_X509_INFO_shift(sk);
      if (xi->x509 != NULL)
      {
        sk_X509_push(stack, xi->x509);
        xi->x509 = NULL;
      }
      X509_INFO_free(xi);
    }
    sk_X509_INFO_free(sk);
  };

  if (sk_X509_num(stack) == 0)
  {
    sk_X509_free(stack);
    stack = NULL;
  }
  return stack;
};

int openssl_sk_x509_read(lua_State*L)
{
  BIO* bio = load_bio_object(L, 1);
  STACK_OF(X509) * certs = load_all_certs_from_file(bio);
  if (certs)
  {
    PUSH_OBJECT(certs, "openssl.stack_of_x509");
  }
  else
  {
    luaL_argerror(L, 1, "error or empty x509 pem file");
    lua_pushnil(L);
  }
  return 1;
}

static int openssl_x509_subject(lua_State* L)
{
  X509* cert = CHECK_OBJECT(1, X509, "openssl.x509");
  if (lua_isnone(L, 2))
  {
    X509_NAME* xn = X509_get_subject_name(cert);
    return openssl_push_xname_asobject(L, xn);
  }else{
    X509_NAME *xn = CHECK_OBJECT(2, X509_NAME, "openssl.x509");
    int ret = X509_set_subject_name(cert, xn);
    return openssl_pushresult(L, ret);
  }
}

static int openssl_x509_issuer(lua_State* L)
{
  X509* cert = CHECK_OBJECT(1, X509, "openssl.x509");
  if (lua_isnone(L, 2)) {
    X509_NAME* xn = X509_get_issuer_name(cert);
    return openssl_push_xname_asobject(L, xn);
  }else {
    X509_NAME* xn = CHECK_OBJECT(2, X509_NAME, "openssl.x509_name");
    int ret = X509_set_issuer_name(cert, xn);
    return openssl_pushresult(L, ret);
  }
}

static int openssl_x509_digest(lua_State* L)
{
  unsigned int bytes;
  unsigned char buffer[EVP_MAX_MD_SIZE];
  char hex_buffer[EVP_MAX_MD_SIZE*2];
  X509 *cert = CHECK_OBJECT(1, X509, "openssl.x509");
  const EVP_MD *digest = get_digest(L, 2);
  if(digest==NULL)
    digest = EVP_sha1();

  if (!digest) {
    lua_pushnil(L);
    lua_pushfstring(L, "digest algorithm not supported (%s)", lua_tostring(L, 2));
    return 2;
  }
  if (!X509_digest(cert, digest, buffer, &bytes)) {
    lua_pushnil(L);
    lua_pushfstring(L, "error processing the certificate (%s)",
      ERR_reason_error_string(ERR_get_error()));
    return 2;
  }
  to_hex((char*)buffer, bytes, hex_buffer);
  lua_pushlstring(L, hex_buffer, bytes*2);
  return 1;
};

static int openssl_x509_notbefore(lua_State *L)
{
  X509* cert = CHECK_OBJECT(1, X509, "openssl.x509");
  if(lua_isnone(L, 2)) {
    return PUSH_ASN1_TIME(L, X509_get_notBefore(cert));
  }else {
    ASN1_TIME* at = NULL;
    int ret = 1;
    if (lua_isnumber(L, 2))
    {
      time_t time = lua_tointeger(L, 2);
      at = ASN1_TIME_new();
      ASN1_TIME_set(at,time);
    }
    if (lua_isstring(L, 2)) {
      const char* time = lua_tostring(L, 2);
      at = ASN1_TIME_new();
      if(ASN1_TIME_set_string(at, time)!=1)
      {
        ASN1_TIME_free(at);
        at = NULL;
      }
    }
    if (at) {
      ret = X509_set_notBefore(cert, at);
    }else
      ret = 0;
    return openssl_pushresult(L, ret);
  }; 
}

static int openssl_x509_notafter(lua_State *L)
{
  X509* cert = CHECK_OBJECT(1, X509, "openssl.x509");
  if (lua_isnone(L, 2)) {
    return PUSH_ASN1_TIME(L, X509_get_notAfter(cert));
  } else {
    ASN1_TIME* at = NULL;
    int ret = 1;
    if (lua_isnumber(L, 2))
    {
      time_t time = lua_tointeger(L, 2);
      at = ASN1_TIME_new();
      ASN1_TIME_set(at,time);
    }
    if (lua_isstring(L, 2)) {
      const char* time = lua_tostring(L, 2);
      at = ASN1_TIME_new();
      if(ASN1_TIME_set_string(at, time)!=1)
      {
        ASN1_TIME_free(at);
        at = NULL;
      }
    }
    if (at) {
      ret = X509_set_notAfter(cert, at);
    }else
      ret = 0;
    return openssl_pushresult(L, ret);
  }
}

static int openssl_x509_valid_at(lua_State* L)
{
  X509* cert = CHECK_OBJECT(1, X509, "openssl.x509");
  if (lua_isnone(L, 2)) {
    time_t now = 0;;
    time(&now);

    lua_pushboolean(L, (X509_cmp_time(X509_get_notAfter(cert), &now)     >= 0
      && X509_cmp_time(X509_get_notBefore(cert), &now) <= 0));
    PUSH_ASN1_TIME(L, X509_get_notBefore(cert));
    PUSH_ASN1_TIME(L, X509_get_notAfter(cert));
    return 3;
  }else if(lua_gettop(L)==2)
  {
    time_t time = luaL_checkinteger(L, 2);
    lua_pushboolean(L, (X509_cmp_time(X509_get_notAfter(cert), &time)     >= 0
      && X509_cmp_time(X509_get_notBefore(cert), &time) <= 0));
    PUSH_ASN1_TIME(L, X509_get_notBefore(cert));
    PUSH_ASN1_TIME(L, X509_get_notAfter(cert));
    return 3;
  }else if(lua_gettop(L)==3)
  {
    time_t before, after;
    ASN1_TIME *ab, *aa;
    int ret = 1;
    before = lua_tointeger(L, 2);
    after  = lua_tointeger(L, 3);

    ab = ASN1_TIME_new();
    aa = ASN1_TIME_new();
    ASN1_TIME_set(ab, before);
    ASN1_TIME_set(aa, after);
    ret = X509_set_notBefore(cert, ab);
    if (ret==1)
        ret = X509_set_notAfter(cert, aa);

    ASN1_TIME_free(ab);
    ASN1_TIME_free(aa);

    return openssl_pushresult(L, ret);
  }
  return 0;
}

static int openssl_x509_serial(lua_State *L)
{
  char *tmp;
  BIGNUM *bn;
  ASN1_INTEGER *serial;
  X509* cert = CHECK_OBJECT(1, X509, "openssl.x509");
  if (lua_isnone(L,2)) {
    serial = X509_get_serialNumber(cert);
    bn = ASN1_INTEGER_to_BN(serial, NULL);
    tmp = BN_bn2hex(bn);
    lua_pushstring(L, tmp);
    BN_free(bn);
    OPENSSL_free(tmp);
    return 1;
  }else {
    int ret;
    bn = BN_get(L, 2);
    serial = BN_to_ASN1_INTEGER(bn, NULL);
    BN_free(bn);
    ret = X509_set_serialNumber(cert, serial);
    ASN1_INTEGER_free(serial);
    return openssl_pushresult(L, ret);
  }
}

static int openssl_x509_version(lua_State *L)
{
  int version;
  X509* cert = CHECK_OBJECT(1, X509, "openssl.x509");
  if (lua_isnone(L,2)) {
    version = X509_get_version(cert);
    lua_pushinteger(L, version);
    return 1;
  }else {
    int ret;
    version = luaL_checkint(L, 2);
    ret = X509_set_version(cert, version);
    return openssl_pushresult(L, ret);
  }
}

static int openssl_x509_extensions(lua_State* L)
{
  X509 *peer = CHECK_OBJECT(1, X509, "openssl.x509");
  if(lua_isnone(L,2))
  {
    STACK_OF(X509_EXTENSION) *exts = peer->cert_info->extensions;
    if(exts) {
      exts = sk_X509_EXTENSION_dup(exts);
      PUSH_OBJECT(exts,"openssl.stack_of_x509_extension");
    }else
      lua_pushnil(L);
    return 1;
  }else {
    STACK_OF(X509_EXTENSION) *exts = CHECK_OBJECT(1, STACK_OF(X509_EXTENSION), "openssl.stack_of_x509_extension");
    int i, n, ret;
    n = sk_X509_EXTENSION_num(exts);
    for(i=0, ret=1; i<n && ret==1; i++)
    {
      X509_EXTENSION *ext = sk_X509_EXTENSION_value(exts, i);
      X509_add_ext(peer, ext, i);
    };
    return openssl_pushresult(L, ret);
  }
  return 0;
}

static int openssl_x509_new(lua_State* L) {
  int i = 1;
  int ret = 1;
  int n = lua_gettop(L);
  X509 *x = X509_new();
  
  ret = X509_set_version(x, 2);
  if (ret == 1 && (
    auxiliar_isclass(L, "openssl.bn", i) ||
    lua_isstring(L,i) || lua_isnumber(L, i)
    ))
  {
    BIGNUM *bn = BN_get(L, i);
    ASN1_INTEGER* ai = BN_to_ASN1_INTEGER(bn, NULL);
    BN_free(bn);
    ret = X509_set_serialNumber(x, ai);
    ASN1_INTEGER_free(ai);
    i++;
  }

  for(;i<=n;i++) {
    if (ret ==1 && auxiliar_isclass(L, "openssl.x509_req", i))
    {
      X509_REQ* csr = CHECK_OBJECT(i, X509_REQ, "openssl.x509_req");
      X509_NAME* xn = X509_REQ_get_subject_name(csr);
      ret = X509_set_subject_name(x, xn);

      if(ret==1) {
        STACK_OF(X509_EXTENSION) *exts = X509_REQ_get_extensions(csr);
        int j,n;
        n = sk_X509_EXTENSION_num(exts);
        for(j=0; ret==1 && j<n; j++) {
          ret = X509_add_ext(x, sk_X509_EXTENSION_value(exts, j), j);
        }
        sk_X509_EXTENSION_pop_free(exts,X509_EXTENSION_free);
      }
      if (ret==1) {
        EVP_PKEY* pkey = X509_REQ_get_pubkey(csr);
        ret = X509_set_pubkey(x,pkey);
        EVP_PKEY_free(pkey);
      }
      i++;
    };

    if(ret==1 && auxiliar_isclass(L, "openssl.x509_name", i))
    {
      X509_NAME *xn = CHECK_OBJECT(i, X509_NAME, "openssl.x509_name");
      ret = X509_set_subject_name(x, xn);
      i++;
    }
    if(ret==1 && auxiliar_isclass(L, "openssl.stack_of_x509_extension", i))
    {
      STACK_OF(X509_EXTENSION) *exts = CHECK_OBJECT(i, STACK_OF(X509_EXTENSION), "openssl.stack_of_x509_extension");
      int j,n;
      n = sk_X509_EXTENSION_num(exts);
      for(j=0; ret==1 && j<n; j++) {
        ret = X509_add_ext(x, sk_X509_EXTENSION_value(exts, j), j);
      }
      i++;
    }
  }

  if (ret==1) {
    PUSH_OBJECT(x, "openssl.x509");
    return 1;
  }else {
    X509_free(x);
    return openssl_pushresult(L, ret);
  }
};

static int openssl_x509_sign(lua_State*L) {
  X509* x = CHECK_OBJECT(1, X509, "openssl.x509");
  EVP_PKEY* pkey = CHECK_OBJECT(2, EVP_PKEY, "openssl.evp_pkey");
  const EVP_MD *md;
  int ret = 1;
  int i = 3;
  if(auxiliar_isclass(L, "openssl.x509_name", 3)){
    X509_NAME* xn = CHECK_OBJECT(3, X509_NAME, "openssl.x509_name");
    ret = X509_set_issuer_name(x, xn);
    i++;
  }else {
    X509* ca = CHECK_OBJECT(3, X509, "openssl.x509");
    X509_NAME* xn = X509_get_subject_name(ca);
    ret = X509_check_private_key(ca,pkey);
    if(ret==1)
    {
      ret = X509_set_issuer_name(x, xn);
    }
    i++;
  }

  if (ret==1) {
    md = lua_isnoneornil(L, i) ? 
      EVP_get_digestbyname("sha1") :
    get_digest(L, i);
    ret = X509_sign(x, pkey, md);
    if(ret==EVP_PKEY_size(pkey))
      ret = 1;
  }
  return openssl_pushresult(L, ret);
}

static luaL_Reg x509_funcs[] =
{
  {"parse",       openssl_x509_parse},
  {"export",      openssl_x509_export},
  {"check",       openssl_x509_check},
  {"pubkey",      openssl_x509_public_key},
  {"version",     openssl_x509_version},

  {"__gc",        openssl_x509_free},
  {"__tostring",  auxiliar_tostring},

  /* compat with luasec */
  {"digest",     openssl_x509_digest},
  {"extensions", openssl_x509_extensions},
  {"issuer",     openssl_x509_issuer},
  {"notbefore",  openssl_x509_notbefore},
  {"notafter",   openssl_x509_notafter},
  {"serial",     openssl_x509_serial},
  {"subject",    openssl_x509_subject},
  {"validat",    openssl_x509_valid_at},

  {"sign",       openssl_x509_sign},

  {NULL,      NULL},
};

static luaL_reg R[] =
{
  {"new",           openssl_x509_new },
  {"read",          openssl_x509_read },
  {"sk_x509_read",  openssl_sk_x509_read },
  {"sk_x509_new",   openssl_sk_x509_new },

  {NULL,    NULL}
};

LUALIB_API int luaopen_x509(lua_State *L)
{
  auxiliar_newclass(L, "openssl.x509", x509_funcs);
  openssl_register_sk_x509(L);

  luaL_register(L, MYNAME, R);

  openssl_register_xname(L);
  lua_setfield(L, -2, "name");
  openssl_register_xattribute(L);
  lua_setfield(L, -2, "attribute");
  openssl_register_xextension(L);
  lua_setfield(L, -2, "extension");

  lua_pushliteral(L, "version");    /** version */
  lua_pushliteral(L, MYVERSION);
  lua_settable(L, -3);

  return 1;
}
