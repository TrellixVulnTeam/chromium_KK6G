// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/cronet/url_request_context_config.h"

#include <memory>

#include "base/json/json_writer.h"
#include "base/strings/string_piece.h"
#include "base/test/scoped_task_environment.h"
#include "base/test/values_test_util.h"
#include "base/values.h"
#include "build/build_config.h"
#include "net/cert/cert_verifier.h"
#include "net/http/http_network_session.h"
#include "net/log/net_log.h"
#include "net/log/net_log_with_source.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_service_fixed.h"
#include "net/url_request/http_user_agent_settings.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(ENABLE_REPORTING)
#include "net/network_error_logging/network_error_logging_service.h"
#include "net/reporting/reporting_service.h"
#endif  // BUILDFLAG(ENABLE_REPORTING)

namespace cronet {

namespace {

base::Value ParseJson(base::StringPiece json) {
  return std::move(*base::test::ParseJson(json));
}

std::string WrapJsonHeader(base::StringPiece value) {
  std::string result;
  result.reserve(value.size() + 2);
  result.push_back('[');
  value.AppendToString(&result);
  result.push_back(']');
  return result;
}

// Returns whether two JSON-encoded headers contain the same content, ignoring
// irrelevant encoding issues like whitespace and map element ordering.
bool JsonHeaderEquals(base::StringPiece expected, base::StringPiece actual) {
  return ParseJson(WrapJsonHeader(expected)) ==
         ParseJson(WrapJsonHeader(actual));
}

}  // namespace

TEST(URLRequestContextConfigTest, TestExperimentalOptionParsing) {
  base::test::ScopedTaskEnvironment scoped_task_environment_(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  // Create JSON for experimental options.
  base::DictionaryValue options;
  options.SetPath({"QUIC", "max_server_configs_stored_in_properties"},
                  base::Value(2));
  options.SetPath({"QUIC", "user_agent_id"}, base::Value("Custom QUIC UAID"));
  options.SetPath({"QUIC", "idle_connection_timeout_seconds"},
                  base::Value(300));
  options.SetPath({"QUIC", "close_sessions_on_ip_change"}, base::Value(true));
  options.SetPath({"QUIC", "race_cert_verification"}, base::Value(true));
  options.SetPath({"QUIC", "connection_options"}, base::Value("TIME,TBBR,REJ"));
  options.SetPath({"AsyncDNS", "enable"}, base::Value(true));
  options.SetPath({"NetworkErrorLogging", "enable"}, base::Value(true));
  options.SetPath({"NetworkErrorLogging", "preloaded_report_to_headers"},
                  ParseJson(R"json(
                  [
                    {
                      "origin": "https://test-origin/",
                      "value": {
                        "group": "test-group",
                        "max_age": 86400,
                        "endpoints": [
                          {"url": "https://test-endpoint/"},
                        ],
                      },
                    },
                    {
                      "origin": "https://test-origin-2/",
                      "value": [
                        {
                          "group": "test-group-2",
                          "max_age": 86400,
                          "endpoints": [
                            {"url": "https://test-endpoint-2/"},
                          ],
                        },
                        {
                          "group": "test-group-3",
                          "max_age": 86400,
                          "endpoints": [
                            {"url": "https://test-endpoint-3/"},
                          ],
                        },
                      ],
                    },
                    {
                      "origin": "https://value-is-missing/",
                    },
                    {
                      "value": "origin is missing",
                    },
                    {
                      "origin": 123,
                      "value": "origin is not a string",
                    },
                    {
                      "origin": "this is not a URL",
                      "value": "origin not a URL",
                    },
                  ]
                  )json"));
  options.SetPath({"NetworkErrorLogging", "preloaded_nel_headers"},
                  ParseJson(R"json(
                  [
                    {
                      "origin": "https://test-origin/",
                      "value": {
                        "report_to": "test-group",
                        "max_age": 86400,
                      },
                    },
                  ]
                  )json"));
  options.SetPath({"UnknownOption", "foo"}, base::Value(true));
  options.SetPath({"HostResolverRules", "host_resolver_rules"},
                  base::Value("MAP * 127.0.0.1"));
  // See http://crbug.com/696569.
  options.SetKey("disable_ipv6_on_wifi", base::Value(true));
  std::string options_json;
  EXPECT_TRUE(base::JSONWriter::Write(options, &options_json));

  URLRequestContextConfig config(
      // Enable QUIC.
      true,
      // QUIC User Agent ID.
      "Default QUIC User Agent ID",
      // Enable SPDY.
      true,
      // Enable Brotli.
      false,
      // Type of http cache.
      URLRequestContextConfig::HttpCacheType::DISK,
      // Max size of http cache in bytes.
      1024000,
      // Disable caching for HTTP responses. Other information may be stored in
      // the cache.
      false,
      // Storage path for http cache and cookie storage.
      "/data/data/org.chromium.net/app_cronet_test/test_storage",
      // Accept-Language request header field.
      "foreign-language",
      // User-Agent request header field.
      "fake agent",
      // JSON encoded experimental options.
      options_json,
      // MockCertVerifier to use for testing purposes.
      std::unique_ptr<net::CertVerifier>(),
      // Enable network quality estimator.
      false,
      // Enable Public Key Pinning bypass for local trust anchors.
      true,
      // Optional network thread priority.
      base::Optional<double>(42.0));

  net::URLRequestContextBuilder builder;
  net::NetLog net_log;
  config.ConfigureURLRequestContextBuilder(&builder, &net_log);
  EXPECT_FALSE(config.effective_experimental_options->HasKey("UnknownOption"));
  // Set a ProxyConfigService to avoid DCHECK failure when building.
  builder.set_proxy_config_service(
      std::make_unique<net::ProxyConfigServiceFixed>(
          net::ProxyConfigWithAnnotation::CreateDirect()));
  std::unique_ptr<net::URLRequestContext> context(builder.Build());
  const net::HttpNetworkSession::Params* params =
      context->GetNetworkSessionParams();
  // Check Quic Connection options.
  quic::QuicTagVector quic_connection_options;
  quic_connection_options.push_back(quic::kTIME);
  quic_connection_options.push_back(quic::kTBBR);
  quic_connection_options.push_back(quic::kREJ);
  EXPECT_EQ(quic_connection_options, params->quic_connection_options);

  // Check Custom QUIC User Agent Id.
  EXPECT_EQ("Custom QUIC UAID", params->quic_user_agent_id);

  // Check max_server_configs_stored_in_properties.
  EXPECT_EQ(2u, params->quic_max_server_configs_stored_in_properties);

  // Check idle_connection_timeout_seconds.
  EXPECT_EQ(300, params->quic_idle_connection_timeout_seconds);

  EXPECT_TRUE(params->quic_close_sessions_on_ip_change);
  EXPECT_FALSE(params->quic_goaway_sessions_on_ip_change);
  EXPECT_FALSE(params->quic_allow_server_migration);
  EXPECT_FALSE(params->quic_migrate_sessions_on_network_change_v2);
  EXPECT_FALSE(params->quic_migrate_sessions_early_v2);
  EXPECT_FALSE(params->quic_retry_on_alternate_network_before_handshake);
  EXPECT_FALSE(params->quic_race_stale_dns_on_connection);

  // Check race_cert_verification.
  EXPECT_TRUE(params->quic_race_cert_verification);

#if defined(ENABLE_BUILT_IN_DNS)
  // Check AsyncDNS resolver is enabled (not supported on iOS).
  EXPECT_TRUE(context->host_resolver()->GetDnsConfigAsValue());
#endif  // defined(ENABLE_BUILT_IN_DNS)

#if BUILDFLAG(ENABLE_REPORTING)
  // Check Reporting and Network Error Logging are enabled (can be disabled at
  // build time).
  EXPECT_TRUE(context->reporting_service());
  EXPECT_TRUE(context->network_error_logging_service());
#endif  // BUILDFLAG(ENABLE_REPORTING)

  ASSERT_EQ(2u, config.preloaded_report_to_headers.size());
  EXPECT_EQ(url::Origin::CreateFromNormalizedTuple("https", "test-origin", 443),
            config.preloaded_report_to_headers[0].origin);
  EXPECT_TRUE(JsonHeaderEquals(  //
      R"json(
      {
        "group": "test-group",
        "max_age": 86400,
        "endpoints": [
          {"url": "https://test-endpoint/"},
        ],
      }
      )json",
      config.preloaded_report_to_headers[0].value));
  EXPECT_EQ(
      url::Origin::CreateFromNormalizedTuple("https", "test-origin-2", 443),
      config.preloaded_report_to_headers[1].origin);
  EXPECT_TRUE(JsonHeaderEquals(  //
      R"json(
      {
        "group": "test-group-2",
        "max_age": 86400,
        "endpoints": [
          {"url": "https://test-endpoint-2/"},
        ],
      },
      {
        "group": "test-group-3",
        "max_age": 86400,
        "endpoints": [
          {"url": "https://test-endpoint-3/"},
        ],
      }
      )json",
      config.preloaded_report_to_headers[1].value));

  ASSERT_EQ(1u, config.preloaded_nel_headers.size());
  EXPECT_EQ(url::Origin::CreateFromNormalizedTuple("https", "test-origin", 443),
            config.preloaded_nel_headers[0].origin);
  EXPECT_TRUE(JsonHeaderEquals(  //
      R"json(
      {
        "report_to": "test-group",
        "max_age": 86400,
      }
      )json",
      config.preloaded_nel_headers[0].value));

  // Check IPv6 is disabled when on wifi.
  EXPECT_TRUE(context->host_resolver()->GetNoIPv6OnWifi());

  net::HostResolver::RequestInfo info(net::HostPortPair("abcde", 80));
  net::AddressList addresses;
  EXPECT_EQ(net::OK, context->host_resolver()->ResolveFromCache(
                         info, &addresses, net::NetLogWithSource()));

  EXPECT_TRUE(config.network_thread_priority);
  EXPECT_EQ(42.0, config.network_thread_priority.value());
}

TEST(URLRequestContextConfigTest, SetQuicServerMigrationOptions) {
  base::test::ScopedTaskEnvironment scoped_task_environment_(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  URLRequestContextConfig config(
      // Enable QUIC.
      true,
      // QUIC User Agent ID.
      "Default QUIC User Agent ID",
      // Enable SPDY.
      true,
      // Enable Brotli.
      false,
      // Type of http cache.
      URLRequestContextConfig::HttpCacheType::DISK,
      // Max size of http cache in bytes.
      1024000,
      // Disable caching for HTTP responses. Other information may be stored in
      // the cache.
      false,
      // Storage path for http cache and cookie storage.
      "/data/data/org.chromium.net/app_cronet_test/test_storage",
      // Accept-Language request header field.
      "foreign-language",
      // User-Agent request header field.
      "fake agent",
      // JSON encoded experimental options.
      "{\"QUIC\":{\"allow_server_migration\":true}}",
      // MockCertVerifier to use for testing purposes.
      std::unique_ptr<net::CertVerifier>(),
      // Enable network quality estimator.
      false,
      // Enable Public Key Pinning bypass for local trust anchors.
      true,
      // Optional network thread priority.
      base::Optional<double>());

  net::URLRequestContextBuilder builder;
  net::NetLog net_log;
  config.ConfigureURLRequestContextBuilder(&builder, &net_log);
  // Set a ProxyConfigService to avoid DCHECK failure when building.
  builder.set_proxy_config_service(
      std::make_unique<net::ProxyConfigServiceFixed>(
          net::ProxyConfigWithAnnotation::CreateDirect()));
  std::unique_ptr<net::URLRequestContext> context(builder.Build());
  const net::HttpNetworkSession::Params* params =
      context->GetNetworkSessionParams();

  EXPECT_FALSE(params->quic_close_sessions_on_ip_change);
  EXPECT_TRUE(params->quic_allow_server_migration);
}

// Test that goaway_sessions_on_ip_change is set on by default for iOS.
#if defined(OS_IOS)
#define MAYBE_SetQuicGoAwaySessionsOnIPChangeByDefault \
  SetQuicGoAwaySessionsOnIPChangeByDefault
#else
#define MAYBE_SetQuicGoAwaySessionsOnIPChangeByDefault \
  DISABLED_SetQuicGoAwaySessionsOnIPChangeByDefault
#endif
TEST(URLRequestContextConfigTest,
     MAYBE_SetQuicGoAwaySessionsOnIPChangeByDefault) {
  base::test::ScopedTaskEnvironment scoped_task_environment_(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  URLRequestContextConfig config(
      // Enable QUIC.
      true,
      // QUIC User Agent ID.
      "Default QUIC User Agent ID",
      // Enable SPDY.
      true,
      // Enable Brotli.
      false,
      // Type of http cache.
      URLRequestContextConfig::HttpCacheType::DISK,
      // Max size of http cache in bytes.
      1024000,
      // Disable caching for HTTP responses. Other information may be stored in
      // the cache.
      false,
      // Storage path for http cache and cookie storage.
      "/data/data/org.chromium.net/app_cronet_test/test_storage",
      // Accept-Language request header field.
      "foreign-language",
      // User-Agent request header field.
      "fake agent",
      // JSON encoded experimental options.
      "{\"QUIC\":{}}",
      // MockCertVerifier to use for testing purposes.
      std::unique_ptr<net::CertVerifier>(),
      // Enable network quality estimator.
      false,
      // Enable Public Key Pinning bypass for local trust anchors.
      true,
      // Optional network thread priority.
      base::Optional<double>());

  net::URLRequestContextBuilder builder;
  net::NetLog net_log;
  config.ConfigureURLRequestContextBuilder(&builder, &net_log);
  // Set a ProxyConfigService to avoid DCHECK failure when building.
  builder.set_proxy_config_service(
      std::make_unique<net::ProxyConfigServiceFixed>(
          net::ProxyConfigWithAnnotation::CreateDirect()));
  std::unique_ptr<net::URLRequestContext> context(builder.Build());
  const net::HttpNetworkSession::Params* params =
      context->GetNetworkSessionParams();

  EXPECT_FALSE(params->quic_close_sessions_on_ip_change);
  EXPECT_TRUE(params->quic_goaway_sessions_on_ip_change);
}

// Tests that goaway_sessions_on_ip_changes can be set on via
// experimental options on non-iOS.
#if !defined(OS_IOS)
#define MAYBE_SetQuicGoAwaySessionsOnIPChangeViaExperimentOptions \
  SetQuicGoAwaySessionsOnIPChangeViaExperimentOptions
#else
#define MAYBE_SetQuicGoAwaySessionsOnIPChangeViaExperimentOptions \
  DISABLED_SetQuicGoAwaySessionsOnIPChangeViaExperimentOptions
#endif
TEST(URLRequestContextConfigTest,
     MAYBE_SetQuicGoAwaySessionsOnIPChangeViaExperimentOptions) {
  base::test::ScopedTaskEnvironment scoped_task_environment_(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  URLRequestContextConfig config(
      // Enable QUIC.
      true,
      // QUIC User Agent ID.
      "Default QUIC User Agent ID",
      // Enable SPDY.
      true,
      // Enable Brotli.
      false,
      // Type of http cache.
      URLRequestContextConfig::HttpCacheType::DISK,
      // Max size of http cache in bytes.
      1024000,
      // Disable caching for HTTP responses. Other information may be stored in
      // the cache.
      false,
      // Storage path for http cache and cookie storage.
      "/data/data/org.chromium.net/app_cronet_test/test_storage",
      // Accept-Language request header field.
      "foreign-language",
      // User-Agent request header field.
      "fake agent",
      // JSON encoded experimental options.
      "{\"QUIC\":{\"goaway_sessions_on_ip_change\":true}}",
      // MockCertVerifier to use for testing purposes.
      std::unique_ptr<net::CertVerifier>(),
      // Enable network quality estimator.
      false,
      // Enable Public Key Pinning bypass for local trust anchors.
      true,
      // Optional network thread priority.
      base::Optional<double>());

  net::URLRequestContextBuilder builder;
  net::NetLog net_log;
  config.ConfigureURLRequestContextBuilder(&builder, &net_log);
  // Set a ProxyConfigService to avoid DCHECK failure when building.
  builder.set_proxy_config_service(
      std::make_unique<net::ProxyConfigServiceFixed>(
          net::ProxyConfigWithAnnotation::CreateDirect()));
  std::unique_ptr<net::URLRequestContext> context(builder.Build());
  const net::HttpNetworkSession::Params* params =
      context->GetNetworkSessionParams();

  EXPECT_FALSE(params->quic_close_sessions_on_ip_change);
  EXPECT_TRUE(params->quic_goaway_sessions_on_ip_change);
}

// Test that goaway_sessions_on_ip_change can be set to false via
// exprimental options on iOS.
#if defined(OS_IOS)
#define MAYBE_DisableQuicGoAwaySessionsOnIPChangeViaExperimentOptions \
  DisableQuicGoAwaySessionsOnIPChangeViaExperimentOptions
#else
#define MAYBE_DisableQuicGoAwaySessionsOnIPChangeViaExperimentOptions \
  DISABLED_DisableQuicGoAwaySessionsOnIPChangeViaExperimentOptions
#endif
TEST(URLRequestContextConfigTest,
     MAYBE_DisableQuicGoAwaySessionsOnIPChangeViaExperimentOptions) {
  base::test::ScopedTaskEnvironment scoped_task_environment_(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  URLRequestContextConfig config(
      // Enable QUIC.
      true,
      // QUIC User Agent ID.
      "Default QUIC User Agent ID",
      // Enable SPDY.
      true,
      // Enable Brotli.
      false,
      // Type of http cache.
      URLRequestContextConfig::HttpCacheType::DISK,
      // Max size of http cache in bytes.
      1024000,
      // Disable caching for HTTP responses. Other information may be stored in
      // the cache.
      false,
      // Storage path for http cache and cookie storage.
      "/data/data/org.chromium.net/app_cronet_test/test_storage",
      // Accept-Language request header field.
      "foreign-language",
      // User-Agent request header field.
      "fake agent",
      // JSON encoded experimental options.
      "{\"QUIC\":{\"goaway_sessions_on_ip_change\":false}}",
      // MockCertVerifier to use for testing purposes.
      std::unique_ptr<net::CertVerifier>(),
      // Enable network quality estimator.
      false,
      // Enable Public Key Pinning bypass for local trust anchors.
      true,
      // Optional network thread priority.
      base::Optional<double>());

  net::URLRequestContextBuilder builder;
  net::NetLog net_log;
  config.ConfigureURLRequestContextBuilder(&builder, &net_log);
  // Set a ProxyConfigService to avoid DCHECK failure when building.
  builder.set_proxy_config_service(
      std::make_unique<net::ProxyConfigServiceFixed>(
          net::ProxyConfigWithAnnotation::CreateDirect()));
  std::unique_ptr<net::URLRequestContext> context(builder.Build());
  const net::HttpNetworkSession::Params* params =
      context->GetNetworkSessionParams();

  EXPECT_FALSE(params->quic_close_sessions_on_ip_change);
  EXPECT_FALSE(params->quic_goaway_sessions_on_ip_change);
}

TEST(URLRequestContextConfigTest, SetQuicConnectionMigrationV2Options) {
  base::test::ScopedTaskEnvironment scoped_task_environment_(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  URLRequestContextConfig config(
      // Enable QUIC.
      true,
      // QUIC User Agent ID.
      "Default QUIC User Agent ID",
      // Enable SPDY.
      true,
      // Enable Brotli.
      false,
      // Type of http cache.
      URLRequestContextConfig::HttpCacheType::DISK,
      // Max size of http cache in bytes.
      1024000,
      // Disable caching for HTTP responses. Other information may be stored in
      // the cache.
      false,
      // Storage path for http cache and cookie storage.
      "/data/data/org.chromium.net/app_cronet_test/test_storage",
      // Accept-Language request header field.
      "foreign-language",
      // User-Agent request header field.
      "fake agent",
      // JSON encoded experimental options.
      "{\"QUIC\":{\"migrate_sessions_on_network_change_v2\":true,"
      "\"migrate_sessions_early_v2\":true,"
      "\"retry_on_alternate_network_before_handshake\":true,"
      "\"max_time_on_non_default_network_seconds\":10,"
      "\"max_migrations_to_non_default_network_on_write_error\":3,"
      "\"max_migrations_to_non_default_network_on_path_degrading\":4}}",
      // MockCertVerifier to use for testing purposes.
      std::unique_ptr<net::CertVerifier>(),
      // Enable network quality estimator.
      false,
      // Enable Public Key Pinning bypass for local trust anchors.
      true,
      // Optional network thread priority.
      base::Optional<double>());

  net::URLRequestContextBuilder builder;
  net::NetLog net_log;
  config.ConfigureURLRequestContextBuilder(&builder, &net_log);
  // Set a ProxyConfigService to avoid DCHECK failure when building.
  builder.set_proxy_config_service(
      std::make_unique<net::ProxyConfigServiceFixed>(
          net::ProxyConfigWithAnnotation::CreateDirect()));
  std::unique_ptr<net::URLRequestContext> context(builder.Build());
  const net::HttpNetworkSession::Params* params =
      context->GetNetworkSessionParams();

  EXPECT_TRUE(params->quic_migrate_sessions_on_network_change_v2);
  EXPECT_TRUE(params->quic_migrate_sessions_early_v2);
  EXPECT_TRUE(params->quic_retry_on_alternate_network_before_handshake);
  EXPECT_EQ(base::TimeDelta::FromSeconds(10),
            params->quic_max_time_on_non_default_network);
  EXPECT_EQ(3,
            params->quic_max_migrations_to_non_default_network_on_write_error);
  EXPECT_EQ(
      4, params->quic_max_migrations_to_non_default_network_on_path_degrading);
}

TEST(URLRequestContextConfigTest, SetQuicStaleDNSracing) {
  base::test::ScopedTaskEnvironment scoped_task_environment_(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  URLRequestContextConfig config(
      // Enable QUIC.
      true,
      // QUIC User Agent ID.
      "Default QUIC User Agent ID",
      // Enable SPDY.
      true,
      // Enable Brotli.
      false,
      // Type of http cache.
      URLRequestContextConfig::HttpCacheType::DISK,
      // Max size of http cache in bytes.
      1024000,
      // Disable caching for HTTP responses. Other information may be stored in
      // the cache.
      false,
      // Storage path for http cache and cookie storage.
      "/data/data/org.chromium.net/app_cronet_test/test_storage",
      // Accept-Language request header field.
      "foreign-language",
      // User-Agent request header field.
      "fake agent",
      // JSON encoded experimental options.
      "{\"QUIC\":{\"race_stale_dns_on_connection\":true}}",
      // MockCertVerifier to use for testing purposes.
      std::unique_ptr<net::CertVerifier>(),
      // Enable network quality estimator.
      false,
      // Enable Public Key Pinning bypass for local trust anchors.
      true,
      // Optional network thread priority.
      base::Optional<double>());

  net::URLRequestContextBuilder builder;
  net::NetLog net_log;
  config.ConfigureURLRequestContextBuilder(&builder, &net_log);
  // Set a ProxyConfigService to avoid DCHECK failure when building.
  builder.set_proxy_config_service(
      std::make_unique<net::ProxyConfigServiceFixed>(
          net::ProxyConfigWithAnnotation::CreateDirect()));
  std::unique_ptr<net::URLRequestContext> context(builder.Build());
  const net::HttpNetworkSession::Params* params =
      context->GetNetworkSessionParams();

  EXPECT_TRUE(params->quic_race_stale_dns_on_connection);
}

TEST(URLRequestContextConfigTest, SetQuicHostWhitelist) {
  base::test::ScopedTaskEnvironment scoped_task_environment_(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  URLRequestContextConfig config(
      // Enable QUIC.
      true,
      // QUIC User Agent ID.
      "Default QUIC User Agent ID",
      // Enable SPDY.
      true,
      // Enable Brotli.
      false,
      // Type of http cache.
      URLRequestContextConfig::HttpCacheType::DISK,
      // Max size of http cache in bytes.
      1024000,
      // Disable caching for HTTP responses. Other information may be stored in
      // the cache.
      false,
      // Storage path for http cache and cookie storage.
      "/data/data/org.chromium.net/app_cronet_test/test_storage",
      // Accept-Language request header field.
      "foreign-language",
      // User-Agent request header field.
      "fake agent",
      // JSON encoded experimental options.
      "{\"QUIC\":{\"host_whitelist\":\"www.example.com,www.example.org\"}}",
      // MockCertVerifier to use for testing purposes.
      std::unique_ptr<net::CertVerifier>(),
      // Enable network quality estimator.
      false,
      // Enable Public Key Pinning bypass for local trust anchors.
      true,
      // Optional network thread priority.
      base::Optional<double>());

  net::URLRequestContextBuilder builder;
  net::NetLog net_log;
  config.ConfigureURLRequestContextBuilder(&builder, &net_log);
  // Set a ProxyConfigService to avoid DCHECK failure when building.
  builder.set_proxy_config_service(
      std::make_unique<net::ProxyConfigServiceFixed>(
          net::ProxyConfigWithAnnotation::CreateDirect()));
  std::unique_ptr<net::URLRequestContext> context(builder.Build());
  const net::HttpNetworkSession::Params* params =
      context->GetNetworkSessionParams();

  EXPECT_TRUE(
      base::ContainsKey(params->quic_host_whitelist, "www.example.com"));
  EXPECT_TRUE(
      base::ContainsKey(params->quic_host_whitelist, "www.example.org"));
}

TEST(URLRequestContextConfigTest, SetQuicMaxTimeBeforeCryptoHandshake) {
  base::test::ScopedTaskEnvironment scoped_task_environment_(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  URLRequestContextConfig config(
      // Enable QUIC.
      true,
      // QUIC User Agent ID.
      "Default QUIC User Agent ID",
      // Enable SPDY.
      true,
      // Enable Brotli.
      false,
      // Type of http cache.
      URLRequestContextConfig::HttpCacheType::DISK,
      // Max size of http cache in bytes.
      1024000,
      // Disable caching for HTTP responses. Other information may be stored in
      // the cache.
      false,
      // Storage path for http cache and cookie storage.
      "/data/data/org.chromium.net/app_cronet_test/test_storage",
      // Accept-Language request header field.
      "foreign-language",
      // User-Agent request header field.
      "fake agent",
      // JSON encoded experimental options.
      "{\"QUIC\":{\"max_time_before_crypto_handshake_seconds\":7,"
      "\"max_idle_time_before_crypto_handshake_seconds\":11}}",
      // MockCertVerifier to use for testing purposes.
      std::unique_ptr<net::CertVerifier>(),
      // Enable network quality estimator.
      false,
      // Enable Public Key Pinning bypass for local trust anchors.
      true,
      // Optional network thread priority.
      base::Optional<double>());

  net::URLRequestContextBuilder builder;
  net::NetLog net_log;
  config.ConfigureURLRequestContextBuilder(&builder, &net_log);
  // Set a ProxyConfigService to avoid DCHECK failure when building.
  builder.set_proxy_config_service(
      std::make_unique<net::ProxyConfigServiceFixed>(
          net::ProxyConfigWithAnnotation::CreateDirect()));
  std::unique_ptr<net::URLRequestContext> context(builder.Build());
  const net::HttpNetworkSession::Params* params =
      context->GetNetworkSessionParams();

  EXPECT_EQ(7, params->quic_max_time_before_crypto_handshake_seconds);
  EXPECT_EQ(11, params->quic_max_idle_time_before_crypto_handshake_seconds);
}

TEST(URLURLRequestContextConfigTest, SetQuicConnectionOptions) {
  base::test::ScopedTaskEnvironment scoped_task_environment_(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  URLRequestContextConfig config(
      // Enable QUIC.
      true,
      // QUIC User Agent ID.
      "Default QUIC User Agent ID",
      // Enable SPDY.
      true,
      // Enable Brotli.
      false,
      // Type of http cache.
      URLRequestContextConfig::HttpCacheType::DISK,
      // Max size of http cache in bytes.
      1024000,
      // Disable caching for HTTP responses. Other information may be stored in
      // the cache.
      false,
      // Storage path for http cache and cookie storage.
      "/data/data/org.chromium.net/app_cronet_test/test_storage",
      // Accept-Language request header field.
      "foreign-language",
      // User-Agent request header field.
      "fake agent",
      // JSON encoded experimental options.
      "{\"QUIC\":{\"connection_options\":\"TIME,TBBR,REJ\","
      "\"client_connection_options\":\"TBBR,1RTT\"}}",
      // MockCertVerifier to use for testing purposes.
      std::unique_ptr<net::CertVerifier>(),
      // Enable network quality estimator.
      false,
      // Enable Public Key Pinning bypass for local trust anchors.
      true,
      // Optional network thread priority.
      base::Optional<double>());

  net::URLRequestContextBuilder builder;
  net::NetLog net_log;
  config.ConfigureURLRequestContextBuilder(&builder, &net_log);
  // Set a ProxyConfigService to avoid DCHECK failure when building.
  builder.set_proxy_config_service(
      std::make_unique<net::ProxyConfigServiceFixed>(
          net::ProxyConfigWithAnnotation::CreateDirect()));
  std::unique_ptr<net::URLRequestContext> context(builder.Build());
  const net::HttpNetworkSession::Params* params =
      context->GetNetworkSessionParams();

  quic::QuicTagVector connection_options;
  connection_options.push_back(quic::kTIME);
  connection_options.push_back(quic::kTBBR);
  connection_options.push_back(quic::kREJ);
  EXPECT_EQ(connection_options, params->quic_connection_options);

  quic::QuicTagVector client_connection_options;
  client_connection_options.push_back(quic::kTBBR);
  client_connection_options.push_back(quic::k1RTT);
  EXPECT_EQ(client_connection_options, params->quic_client_connection_options);
}

TEST(URLURLRequestContextConfigTest, SetAcceptLanguageAndUserAgent) {
  base::test::ScopedTaskEnvironment scoped_task_environment_(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  URLRequestContextConfig config(
      // Enable QUIC.
      true,
      // QUIC User Agent ID.
      "Default QUIC User Agent ID",
      // Enable SPDY.
      true,
      // Enable Brotli.
      false,
      // Type of http cache.
      URLRequestContextConfig::HttpCacheType::DISK,
      // Max size of http cache in bytes.
      1024000,
      // Disable caching for HTTP responses. Other information may be stored in
      // the cache.
      false,
      // Storage path for http cache and cookie storage.
      "/data/data/org.chromium.net/app_cronet_test/test_storage",
      // Accept-Language request header field.
      "foreign-language",
      // User-Agent request header field.
      "fake agent",
      // JSON encoded experimental options.
      "{}",
      // MockCertVerifier to use for testing purposes.
      std::unique_ptr<net::CertVerifier>(),
      // Enable network quality estimator.
      false,
      // Enable Public Key Pinning bypass for local trust anchors.
      true,
      // Optional network thread priority.
      base::Optional<double>());

  net::URLRequestContextBuilder builder;
  net::NetLog net_log;
  config.ConfigureURLRequestContextBuilder(&builder, &net_log);
  // Set a ProxyConfigService to avoid DCHECK failure when building.
  builder.set_proxy_config_service(
      std::make_unique<net::ProxyConfigServiceFixed>(
          net::ProxyConfigWithAnnotation::CreateDirect()));
  std::unique_ptr<net::URLRequestContext> context(builder.Build());
  EXPECT_EQ("foreign-language",
            context->http_user_agent_settings()->GetAcceptLanguage());
  EXPECT_EQ("fake agent", context->http_user_agent_settings()->GetUserAgent());
}

// See stale_host_resolver_unittest.cc for test of StaleDNS options.

}  // namespace cronet
