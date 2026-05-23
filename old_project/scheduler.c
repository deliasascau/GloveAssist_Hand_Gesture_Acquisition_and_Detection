// ============================================================================
// SCHEDULER.C v2.3 - Task Scheduler + Timer0 ISR (1ms tick @ 16MHz/64/250)
// ============================================================================
// Timer0 CTC mode: (F_CPU/prescaler/freq)-1 = (16MHz/64/1000Hz)-1 = 249
// ISR @ 1ms → system_millis++, Tasks (max 8): periodic/one-shot, debounce
// States: BOOT → DETECTING → GESTURE_FOUND → DETECTING loop
// ============================================================================

#include "scheduler.h"

static task_t tasks[MAX_TASKS];
static uint8_t task_count = 0;
static volatile uint32_t system_millis = 0;
static system_state_t current_state = STATE_BOOT;

// Timer0 Compare Match A interrupt @ 1ms (CTC mode, prescaler 64, OCR0A=249)
ISR(TIMER0_COMPA_vect) {
    system_millis++;
}

void scheduler_init(void) {
    for (uint8_t i = 0; i < MAX_TASKS; i++) {
        tasks[i].callback = 0;
        tasks[i].period_ms = 0;
        tasks[i].last_run_ms = 0;
        tasks[i].priority = PRIORITY_LOW;
        tasks[i].enabled = 0;
    }
    task_count = 0;
    
    TCCR0A = (1 << WGM01);               // CTC mode
    TCCR0B = (1 << CS01) | (1 << CS00);  // Prescaler 64
    OCR0A = 249;                         // 1ms tick
    TIMSK0 = (1 << OCIE0A);              // Enable interrupt
    SREG |= (1 << 7);                    // Global interrupts ON
}

int8_t scheduler_add_task(task_callback_t callback, uint32_t period_ms, task_priority_t priority) {
    if (task_count >= MAX_TASKS || callback == 0) return -1;
    
    for (uint8_t i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].callback == 0) {
            uint8_t sreg = SREG;
            SREG &= ~(1 << 7);  // cli
            
            tasks[i].callback = callback;
            tasks[i].period_ms = period_ms;
            tasks[i].last_run_ms = system_millis;
            tasks[i].priority = priority;
            tasks[i].enabled = 1;
            task_count++;
            
            SREG = sreg;  // sei
            return i;
        }
    }
    return -1;
}

void scheduler_run(void) {
    // 1. CITIRE ATOMICĂ system_millis
    uint8_t sreg = SREG;        // Salvează starea SREG
    SREG &= ~(1 << 7);          // cli() - Disable interrupts
    uint32_t current_ms = system_millis;
    SREG = sreg;                // Restaurează SREG (sei dacă era activ)
    
    // 2. ITERARE PRIN TOATE TASK-URILE
    for (uint8_t i = 0; i < MAX_TASKS; i++) {
        // Skip task-uri invalide/dezactivate
        if (tasks[i].callback == 0 || !tasks[i].enabled) continue;
        
        // 3. CALCUL TIMP SCURS
        uint32_t elapsed_ms = current_ms - tasks[i].last_run_ms;
        
        // 4. TASK ONE-SHOT (period_ms == 0)
        if (tasks[i].period_ms == 0) {
            tasks[i].callback();      // Execută task
            tasks[i].callback = 0;    // Marchează slot LIBER
            tasks[i].enabled = 0;     // Dezactivează
            task_count--;             // Decrementează număr tasks
        } 
        // 5. TASK PERIODIC (elapsed >= period)
        else if (elapsed_ms >= tasks[i].period_ms) {
            tasks[i].callback();      // Execută task
            tasks[i].last_run_ms = current_ms;  // Update timestamp
        }
    }
}

uint32_t scheduler_millis(void) {
    uint8_t sreg = SREG;
    SREG &= ~(1 << 7);
    uint32_t ms = system_millis;
    SREG = sreg;
    return ms;
}

void scheduler_delay_ms(uint32_t ms) {
    uint32_t start = scheduler_millis();
    while ((scheduler_millis() - start) < ms) {
        scheduler_run();
    }
}

system_state_t scheduler_get_state(void) {
    return current_state;
}

void scheduler_set_state(system_state_t state) {
    current_state = state;
}

// Watchdog: auto-reset @ 1s if no wdt_reset() (protecție loop freeze)
void scheduler_watchdog_init(void) {
    cli();
    wdt_reset();
    WDTCSR |= (1 << WDCE) | (1 << WDE);
    WDTCSR = (1 << WDE) | (1 << WDP2) | (1 << WDP1);  // 1s timeout
    sei();
}

void scheduler_watchdog_reset(void) {
    wdt_reset();
}

void scheduler_watchdog_disable(void) {
    cli();
    wdt_reset();
    WDTCSR |= (1 << WDCE) | (1 << WDE);
    WDTCSR = 0;
    sei();
}

uint8_t scheduler_check_reset_source(void) {
    uint8_t mcusr = MCUSR;
    MCUSR = 0;
    if (mcusr & (1 << WDRF)) return 1;       // Watchdog reset
    if (mcusr & (1 << BORF)) return 2;       // Brown-out
    if (mcusr & (1 << EXTRF)) return 3;      // External reset
    if (mcusr & (1 << PORF)) return 0;       // Power-on
    return 0;
}