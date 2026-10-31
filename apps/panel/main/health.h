#pragma once

#include <stdint.h>

#include "db_proto.h"
#include "esp_err.h"

/*
 * Module health reports (ADR 0015): every report is logged; with CONFIG_DB_HEARTBEAT a registered module that sends
 * no report for 3 intervals is logged as missing, once per interval, until its reports resume.
 */

/**
 * @brief Start supervision; registry_init() must run first.
 *
 * @param restart_due  called in the esp_timer task when no module has reported for too long
 *                     (CONFIG_DB_RESTART_WHEN_SILENT); the application then calls health_restart_for_silence() from a
 *                     task that may block
 */
esp_err_t health_init(void (*restart_due)(void));

/** @brief Count the silent-network restart in NVS and restart the panel (ADR 0016). Does not return. */
void health_restart_for_silence(void);

/** @brief Erase the stored supervision state (silent-network restart count); building reset. */
esp_err_t health_erase(void);

/** @brief Log a report of a registered apartment and mark it as heard. Runs in the protocol task. */
void health_on_report(uint8_t apartment, const db_health_t *report);

/** @brief Start supervising a newly paired apartment from now. */
void health_on_paired(uint8_t apartment);
