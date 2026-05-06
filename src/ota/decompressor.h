/**
 * @file    src/ota/decompressor.h
 * @brief   Wrapper one-shot pull-based sobre uzlib (Fase 3 OTA).
 *
 * @details Divergência do plano §6 Fase 3: uzlib NÃO SUPORTA streaming
 *          push verdadeiro — uma vez que `source` esgote mid-decode, o
 *          estado interno corrompe e não retoma (verificado empiricamente
 *          em 5/2026). Substituímos a API push (`feed(chunk)`) por uma
 *          API pull (`decompress(src_cb)`), onde o caller fornece um
 *          callback que devolve o próximo byte sob demanda.
 *
 *          Implicações:
 *          - Fase 5 (upload): acumular o .bin.gz inteiro em LittleFS antes
 *            de descomprimir; src_cb lê do File.
 *          - Fase 7 (apply): src_cb lê do staging via XIP byte-a-byte
 *            (operação rara, latência aceitável).
 *
 *          GunzipContext é grande (~33 KB de janela LZ77 + state). Esperado
 *          alocar UMA instância estática (BSS) por vida do programa.
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT (wrapper) / zlib (uzlib upstream)
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

extern "C" {
#include "uzlib.h"
}

namespace ota {

/* Tamanho máximo da janela LZ77 do deflate (32 KB pelo RFC 1951). */
constexpr size_t GUNZIP_DICT_SIZE = 32768;

/**
 * @brief Source callback pull-based.
 *
 * Deve retornar o próximo byte (0..255) ou **-1 em EOF real** (não em
 * "esperando mais"). Modelo é síncrono — se há fonte assíncrona, acumular
 * antes de chamar decompress().
 */
typedef int (*GunzipSourceCb)(void* user);

/**
 * @brief Output callback. Retornar false aborta o decompressor.
 */
typedef bool (*GunzipOutCb)(const uint8_t* data, size_t len, void* user);

struct GunzipContext {
    struct uzlib_uncomp uc;          /**< Primeiro membro — cast (uc*)→(GunzipContext*). */
    unsigned char dict[GUNZIP_DICT_SIZE];
    GunzipSourceCb src_cb;
    void*          src_user;
    int            last_status;      /**< TINF_DONE em sucesso. */
    bool           finished;
    bool           out_aborted;
};

/**
 * @brief Inicializa contexto. Idempotente; pode ser chamado pra reset.
 */
bool gunzip_begin(GunzipContext& ctx);

/**
 * @brief Descomprime gzip integral em uma chamada (pull-based).
 *
 * Lê via @p src_cb até EOF, descomprime, emite via @p out_cb. Não retorna
 * até consumir o stream inteiro OU encontrar erro.
 *
 * @param ctx         Contexto (já inicializado por gunzip_begin).
 * @param src_cb      Callback que devolve próximo byte ou -1 em EOF.
 * @param src_user    Pointer opaco passado a src_cb.
 * @param out_cb      Callback que recebe chunks descomprimidos.
 * @param out_user    Pointer opaco passado a out_cb.
 * @return true se completou com sucesso (TINF_DONE + trailer válido).
 */
bool gunzip_decompress(GunzipContext& ctx,
                       GunzipSourceCb src_cb, void* src_user,
                       GunzipOutCb out_cb, void* out_user);

/**
 * @brief Verifica que o stream foi descomprimido corretamente.
 *
 * Equivalente a checar o retorno de gunzip_decompress. Mantido por
 * simetria com a API do plano.
 */
bool gunzip_finish(GunzipContext& ctx);

} /* namespace ota */
