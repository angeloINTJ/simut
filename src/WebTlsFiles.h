/**
 * @file WebTlsFiles.h
 * @brief Where the server's TLS pair lives — one spelling, two readers.
 *
 * @details The paths were spelled inline in WebManager_Core.cpp while it was
 * the only file that opened them. /api/tls (WebManager_Tls.cpp) is the second,
 * and two copies of a path that must agree is how a pair gets written to one
 * place and read from another.
 *
 * Under /config on purpose, and it is the reason /api/tls exists at all:
 * isSecretFsPath( ) keeps /download away from the private key, `system format`
 * clears it with the rest of the credential store, and the OTA backup carries
 * it — while handleUploadData( ) refuses every upload that resolves there, so
 * the pair needs a route of its own. See docs/AUTHORIZATION.md and issue #133.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

/* PEM. What the operator generates, and what both readers expect:
 *   openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 \
 *     -keyout web_key.pem -out web_cert.pem -days 3650 -nodes -subj "/CN=simut"
 * EC (P-256) over RSA on purpose — a P-256 handshake fits this heap far more
 * comfortably than RSA-2048. */
#define FILE_WEB_CERT "/config/web_cert.pem"
#define FILE_WEB_KEY  "/config/web_key.pem"
