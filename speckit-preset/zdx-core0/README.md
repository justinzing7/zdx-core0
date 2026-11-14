# ZDX CORE-0 Spec Kit preset

Adapts GitHub Spec Kit's Spec-Driven Development flow to Zingcore firmware built on ZDX CORE-0.

| File | Strategy | Effect |
|---|---|---|
| `constitution-template.md` | replace | The ten CORE-0 principles become the project constitution |
| `spec-addendum.md` | append → spec-template | Device context, interaction flow (logical keys), power/timing expectations, failure behavior, HW acceptance criteria |
| `plan-addendum.md` | append → plan-template | Foundation mapping, new modules/screens/events, pin changes, RAM/flash/power/timing budgets, constitution gate |
| `tasks-addendum.md` | append → tasks-template | Bring-up ordering, test-first native tasks, hardware verification and closing tasks |
| `checklist-addendum.md` | append → checklist-template | CORE-0 firmware quality checks |

Install into a project that was initialized with `specify init`:

```bash
specify preset add --dev path/to/zdx-core0/speckit-preset/zdx-core0
specify preset list                       # confirm it is enabled
specify preset resolve plan-template      # see the composed template
```

Then run `/speckit-constitution` in Claude Code. The constitution template is pre-filled, so the agent only fills the placeholders: project name, MCU/board, profile, and owner.

To update after editing the preset: `specify preset update zdx-core0 --dev path/to/...`
