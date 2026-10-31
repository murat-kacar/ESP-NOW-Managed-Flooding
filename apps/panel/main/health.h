#pragma once

#include <stdint.h>

#include "db_proto.h"
#include "esp_err.h"

/*
 * Module health reports (ADR 0015): every report is logged; with CONFIG_DB_HEARTBEAT a registered module that sends
 * no report for 3 intervals is logged as missing, once per interval, until its reports resume.
 */

/** @brief Start supervision; registry_init() must run first. */
esp_err_t health_init(void);

/** @brief Erase the stored supervision state (silent-network restart count); building reset. */
esp_err_t health_erase(void);

/** @brief Log a report of a registered apartment and mark it as heard. Runs in the protocol task. */
void health_on_report(uint8_t apartment, const db_health_t *report);

/** @brief Start supervising a newly paired apartment from now. */
void health_on_paired(uint8_t apartment);
