/**
 * @file security.h
 * @brief BLE payload obfuscation, honeypot generation, anti-replay API (STM32)
 */

#ifndef SECURITY_H
#define SECURITY_H

#include <stdint.h>
#include "frame_protocol.h"

/**
 * @brief Initialise security subsystem (XOR key rotation timer, lockdown state).
 * @return 0 on success.
 */
int security_init(void);

/**
 * @brief XOR-obfuscate a payload buffer with the rotating key.
 *
 * @param payload  Buffer to obfuscate in-place.
 * @param len      Number of bytes to process.
 */
void security_xor_payload(uint8_t *payload, uint8_t len);

/**
 * @brief Build a randomised honeypot protocol frame.
 *
 * @param frame  Output frame (must not be NULL).
 * @return 0 on success, negative on error.
 */
int security_gen_honeypot(glove_frame_t *frame);

/**
 * @brief Validate a received protocol frame and track consecutive invalid frames.
 *
 * Triggers lockdown after LOCKDOWN_INVALID_FRAMES consecutive failures.
 *
 * @param frame  Received frame.
 * @return 0 if valid, ERR_SECURITY_LOCKDOWN or CRC error code otherwise.
 */
int security_check_frame(const glove_frame_t *frame);

#endif /* SECURITY_H */
