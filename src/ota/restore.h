/**
 * @file src/ota/restore.h
 * @brief State machine de restore de backup .bkp via streaming chunks.
 *
 * @details Diferente de backup_validate (que precisa de Stream& seekable),
 * o restore consome chunks vindos do upload HTTP (callback
 * handleUploadData) e mantém estado entre chunks.
 *
 * Modo VALIDATE: lê + computa CRCs + valida; nunca toca LittleFS.
 * Modo APPLY: durante CONTENT, escreve em "<path>.restore_tmp";
 * no commit final, renomeia todos para o path original.
 * Em rollback, deleta todos `.restore_tmp` órfãos.
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "backup.h"
#include "backup_format.h"
#include <LittleFS.h>

namespace ota {

constexpr const char* RESTORE_TMP_SUFFIX = ".restore_tmp";
constexpr size_t RESTORE_MAX_PATH = 200; /* < 256 (path_len uint16) e cabe no struct */

enum class RestoreMode : uint8_t {
 VALIDATE = 0, /**< Não escreve em LittleFS. */
 APPLY = 1, /**< Escreve em <path>.restore_tmp; commit/rollback no final. */
};

enum class RestorePhase : uint8_t {
 HEADER = 0, /**< Acumulando 40 bytes do BackupHeader. */
 ENTRY_HEADER = 1, /**< Acumulando 6 bytes do BackupEntry. */
 PATH = 2, /**< Acumulando path_len bytes do path. */
 CONTENT = 3, /**< Acumulando/escrevendo content_len bytes. */
 DONE = 4, /**< payload_size completo, CRC validado. */
 FAILED = 5, /**< Erro detectado; status conta o porquê. */
};

struct RestoreSession {
 RestoreMode mode;
 RestorePhase phase;
 BackupStatus status;

 /* HEADER phase */
 uint8_t header_buf[sizeof(BackupHeader)];
 uint32_t header_filled;
 BackupHeader header;

 /* PAYLOAD phase */
 uint32_t payload_remaining;
 uint32_t payload_crc;
 uint16_t file_count;

 /* ENTRY phase */
 uint8_t entry_buf[6];
 uint32_t entry_filled;
 uint16_t cur_path_len;
 uint32_t cur_content_len;

 /* PATH phase */
 char cur_path[RESTORE_MAX_PATH + 1]; /* +1 nul */
 uint32_t cur_path_filled;

 /* CONTENT phase */
 File cur_file; /**< Aberto só em APPLY. */
 uint32_t cur_content_remaining;
};

/**
 * @brief Inicializa sessão. Em APPLY: faz cleanup de .restore_tmp órfãos.
 */
void restore_session_begin(RestoreSession& s, RestoreMode mode);

/**
 * @brief Alimenta um chunk de bytes. Pode ser chamada várias vezes.
 *
 * Se o estado entrar em FAILED, chamadas subsequentes são no-op.
 *
 * @return true se ainda não falhou (FAILED ou DONE não diferenciados aqui).
 */
bool restore_session_feed(RestoreSession& s, const uint8_t* data, size_t len);

/**
 * @brief Finaliza a sessão.
 *
 * Em VALIDATE: apenas reporta status (sem efeitos colaterais).
 * Em APPLY:
 * - Se status == OK e phase == DONE: commit (rename todos .restore_tmp).
 * - Caso contrário: rollback (deleta todos .restore_tmp).
 *
 * @param s Sessão.
 * @param fs_modified Out: true se a LittleFS foi modificada (apenas APPLY+commit).
 * @return Status final.
 */
BackupStatus restore_session_finish(RestoreSession& s, bool* fs_modified);

/**
 * @brief Aborta a sessão (cleanup de tmps + reset).
 *
 * Para uso em UPLOAD_FILE_ABORTED.
 */
void restore_session_abort(RestoreSession& s);

/**
 * @brief Cleanup standalone de .restore_tmp órfãos (chamável a qualquer momento).
 *
 * Útil em boot e antes de qualquer nova sessão APPLY. Walk recursivo.
 *
 * @return Quantidade de arquivos removidos.
 */
uint32_t restore_cleanup_orphan_tmps( );

} /* namespace ota */
