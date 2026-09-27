# MAX-FX level triggers, FSM messages, and sound pitch

Runtime semantics decoded from the Android `libMaxPayne.so` decompile and
implemented in `src/maxfx/game/Runtime.cpp`. Trigger kinds and names agree
with the PC LDB (verified against `docs/Part1_Level1.ldb`: 131 triggers,
536 FSMs).

## Trigger entity (`X_LevelRuntimeTrigger`)

The LDB stores `{sharedName, properties, radius, type}`. At runtime a
trigger is also an `X_LevelRuntimeFSM` linked to the level FSM **named like
the trigger without the `.TRIGGER` suffix** (`X_LevelDBTrigger::getFSM`).
On Part1_Level1 all 131 triggers match a level FSM by that rule, e.g.

```
trigger  ::2nd_rail::control_desk::trigger.TRIGGER   (type 0, r 0.40)
fsm      ::2nd_rail::control_desk::trigger           entity event "T_Activate"
```

State (offsets in the decompile):

| Offset | Field | Meaning |
| --- | --- | --- |
| `+1612` | `enabled` | `T_Enable(bool)` sets it; disabled triggers skip collision |
| `+1613` | `activated` | latch set by `activate()`; one fire per arm |

### Types (`isAutoActivate`, `X_Character::canActivate`)

| Type | Kind | Fires |
| --- | --- | --- |
| 0 | action button | on **Use** while inside (any character) |
| 1 | player collide | on capsule **entry**, player only |
| 2 | projectile | damaging non-debris projectile path crosses the sphere |
| 3 | character collide | on capsule entry, any character (player or NPC) |
| 4 | look-at | while the player looks at it from inside |

Types 1/3/4 are auto-activate; type 0 waits for the use key.

### `activate()` / `T_Activate`

```c
if (enabled && !activated) {
    handleMessages(getOtherMessages("T_Activate"), currentState, activator);
    activated = 1;                  // latch — fires once
}
```

`T_Activate` goes to the trigger's **own** FSM only — never broadcast. The
FSM's entity event runs before → state-specific (current state) → after.
Authored levels make triggers one-shot by listing
`<self>.TRIGGER->T_Enable(false)` inside their own `T_Activate`.

### `T_Enable(bool)` (`X_LevelRuntimeTrigger::receive`)

Sets `enabled`, updates the collision flags and **always resets the latch**
(`+1613 = 0`), so `T_Enable(false)` disarms and `T_Enable(true)` re-arms.

Real wiring from Part1_Level1:

```
::2nd_rail::control_desk::trigger  T_Activate:
    ::2nd_rail::control_desk::trigger.TRIGGER->T_Enable(false);   // self-disarm
    ::2nd_rail::control_desk::button2.DO->DO_Animate(go_down);    // targeted door
    ::2nd_rail::button_lookat.TRIGGER->T_Enable(false);           // disarm other
    ::p2::electricity_controller->FSM_Send(start);                // cross-room FSM

::bridget01::p5_train_on  T_Activate:
    ::bridget01::p5_train_on.TRIGGER->T_Enable(false);
    ::bridget01::p5_train_off.TRIGGER->T_Enable(true);            // hand-over
```

## FSM message routing

Message form is `target->Method(args)`. The runtime resolves targets:

| Method | Target | Effect |
| --- | --- | --- |
| `T_Activate` | `X.TRIGGER` | activate that trigger (chain) |
| `T_Enable(bool)` | `X.TRIGGER` | set `enabled`, reset the latch |
| `FSM_Switch(state)` | FSM | switch current state (gates state-specific lists) |
| `FSM_Send(string)` | FSM | run that FSM's custom-string event |
| `DO_Animate` / `DO_InvertAnimation` / `DO_Stop/Pause/ResumeAnimation` | `X.DO` | animate that dynamic mesh (name match, first-idle-door fallback) |
| `MPGNM_PickUpNote(id)` | `MaxPayne_GraphicNovelMode` | queue the comic page, open the reader |
| `s_modeswitch(graphicnovel)` | `x_modeswitch` | enter the reader at the first picked note |

Every FSM runs its **startup** messages at level load
(`GameRuntime::startLevel`). On Part1_Level1 `::startroom::fsm_start` picks
the four intro pages and switches to the reader — the intro comic plays once
per level load, then gameplay resumes. The picked-note queue is popped one
page per reader exit.

## Sound pitch (`X_SoundFactory<X_SoundOmni>::getSound`)

```c
Pitch = X_SharedDBSoundSound::getPitch(sound);          // script field, Hz
setFrequency(Pitch / S_Sound::getSamplesPerSecond(sound));  // / WAV rate
```

The script `Pitch` is a **playback rate in Hz divided by the file's own
sample rate** (`soundPitchMultiplier()` in `src/maxfx/sound/Sound.h`).
`Pitch` unset means native rate. A hardcoded `/22050` divisor made every
44.1 kHz cue — graphic-novel narration — play at double speed.
