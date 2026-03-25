#pragma once

/**
 * @brief Initialize hardware and start the radar scanning task.
 *        The task sweeps the servo from 0° to 180° and back,
 *        measuring distance at each step and broadcasting the
 *        result via WebSocket.
 */
void radar_start(void);