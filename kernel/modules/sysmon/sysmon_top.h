#ifndef SYSMON_TOP_H
#define SYSMON_TOP_H

/**
 * System Monitor (top) - K-ABI Module
 * 
 * Provides real-time system monitoring similar to Linux's `top` command.
 * Displays process information, CPU usage, memory statistics, and uptime.
 */

/**
 * @brief Initialize the sysmon module. Must be called during kernel boot.
 */
void sysmon_init(void);

/**
 * @brief Display a snapshot of current system status.
 * Shows process table, memory usage, and uptime.
 */
void sysmon_top(void);

/**
 * @brief Display detailed process information.
 */
void sysmon_ps_verbose(void);

/**
 * @brief Display memory statistics.
 */
void sysmon_mem_stats(void);

#endif // SYSMON_TOP_H
