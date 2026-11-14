# Core: module registry, loop, event bus, timers, port

Headers: `module.h`, `event.h`, `timer.h`, `ring.h`, `port.h`, `types.h`, `config.h`

## Module registry

- Modules are `const Module` tables of optional hooks: `init`, `tick`, `next_deadline`, `can_enter`, `on_suspend`, `on_resume`. Any hook may be `nullptr`.
- `sys::register_module()` is called in `setup()` before `sys::begin()`. The registry is a static array of size `CORE0_MAX_MODULES`.
- `sys::begin()` calls each module's `init()` in order:
  - A non-`Ok` result marks the module **faulted**: it is skipped in `tick()` and the power hooks, it is logged at E, and `FaultCode::ModuleInitFailed` is posted.
  - Boot always continues. A dead display must not prevent the CLI from working.
- `sys::run_once()` follows the sequence in [architecture §2](../00-architecture.md#2-runtime-model). It measures each `tick()` with `hal::micros()`; a tick longer than 5 ms posts `FaultCode::ModuleOverrun` (rate-limited).
- `fault::note_module(i, name)` is set before each tick, so a watchdog reset can name the module that hung. The name is copied into the record, because the module list can differ on the next boot (safe mode).

## Event bus

- One static MPSC queue of `CORE0_EVENT_QUEUE_LEN` events (power of two), guarded by a short critical section on `post()`. This makes `post()` safe from any ISR or task.
- `subscribe(type, fn, ctx)` happens at init only. `EventType::None` subscribes to everything, which the CLI and fault recorder use.
- `dispatch(budget)` delivers at most `CORE0_DISPATCH_BUDGET` events per loop pass, in FIFO order, to subscribers in subscription order. Events posted while dispatching are delivered in a later pass, never recursively.
- When the queue is full, `post()` returns `NoSpace` and increments `dropped_count()`. The bus posts `FaultCode::EventDropped` once the queue has room again. Dropping an event is a bug to fix by resizing the queue or reducing chatter; it is never silently ignored.
- The UI module subscribes to `Input` and forwards those events to the top screen. Screens get other event types only through `ui::forward()`.

## Soft timers

- Fixed pool of `CORE0_MAX_TIMERS`. A timer either calls a callback or posts an event; both run in loop context.
- Periodic timers re-arm from their previous deadline, so they don't drift. If the loop falls behind by more than one period, missed periods are skipped rather than fired in a burst.
- `timers::next_deadline()` feeds `sys::next_deadline()`, which sizes idle sleeps.
- After Light or Deep sleep, the port reports how long it slept so `timers::compensate_sleep(ms)` can advance timers whose clock stopped during sleep.
- `stop()` frees the slot, so the id is invalid afterwards. To resume, start a new timer.
- Use a timer for "do X in N ms" and for periodic sampling. Use a module `tick()` state machine for multi-step sequences.

## SPSC ring

`SpscRing<T, N>` (`ring.h`) is fully implemented in the header. It is the only structure allowed to cross ISR→loop or core0↔core1 without a lock: exactly one producer and one consumer, with memory barriers via `CORE0_MEMORY_BARRIER()`.

## Port

`port.h` lists everything MCU-specific: time, critical sections, sleep, reset reason, watchdog, NVM bytes, console, chip ID, and starting core 1. Each target implements one file `src/port/port_<mcu>.cpp` (later shared in a ports library).

| Function | ESP32 | SAMD21 | STM32 | RP2040 | native |
|---|---|---|---|---|---|
| `critical_enter` | `portENTER_CRITICAL` (spinlock) | `__disable_irq` + nest count | `__disable_irq` + nest | `spin_lock_blocking` / `noInterrupts` | mutex/no-op |
| `sleep(Idle)` | `esp_light_sleep` w/ timer or `vTaskDelay` | `__WFI` | `__WFI` | `__wfi` | `usleep` |
| `sleep(Light)` | `esp_light_sleep_start` | STANDBY + RTC | STOP2 | dormant/sleep | — |
| `sleep(Deep)` | `esp_deep_sleep_start` (resets) | STANDBY, RAM off | STANDBY (resets) | dormant | — |
| `nvm_*` | `Preferences` blob | FlashStorage emu | EEPROM emu | EEPROM emu (flash) | file |
| `reset_reason` | `esp_reset_reason` | `PM->RCAUSE` | `RCC->CSR` | `watchdog_caused_reboot` | — |

## Acceptance

- [ ] Registering more than `CORE0_MAX_MODULES` modules returns `NoSpace`, and `begin()` still runs the rest.
- [ ] A module whose `init()` fails is skipped, and the remaining modules tick.
- [ ] 1000 events posted from a timer ISR at 1 kHz are all delivered, in order, with `dropped_count() == 0` at default sizing.
- [ ] A periodic timer at 100 ms shows less than 1 ms cumulative drift over 10 minutes (native test with a mocked clock).
- [ ] `time_reached` behaves correctly across the `millis()` wrap (native test).
- [ ] The watchdog resets the device when a test module spins in `tick()`, and `fault::last().module_name` names that module.
