# GGSMDriver — handoff

Updated 2026-10-17. **Milestone 4 in progress**: fab files generated, firmware next.

This folder is a standalone repo (moved out of `gustavgaston/works` on 2026-10-17).
Paths below are relative to the repo root. Commands run from an `autopcb`
checkout, with `GGSM` set to this repo's path (e.g. `GGSM=../GGSMDriver`).

## Agent instructions

- Module work (design, docs, fab files, firmware) goes in this repo; generator
  code goes in `gustavgaston/autopcb`.
- Never guess: ask Gustav when a value or choice is unclear.
- Pause at each milestone in `PLAN.md` for Gustav to evaluate.
- Keep it lean (artistic practice); use smaller models for busywork/code.
- Record confirmed decisions in `requirements.md`, current state here.

## State

- Schematic source: `hardware/ggsmdriver.py` (parts + pin→net map, LCSC numbers).
  Regenerate from the autopcb checkout:
  `python3 -m pcbai.schgen $GGSM/hardware/ggsmdriver.py $GGSM/kicad`
  Output: `kicad/ggsmdriver.kicad_pro/.kicad_sch`, `kicad/ggsmdriver.pdf`, `kicad/reports/`.
  Edits made by hand in KiCad are overwritten on regeneration; change the .py instead
  (or stop regenerating once Gustav takes over the schematic).
- PCB: `layout(sch)` in the same .py places parts (fixed spots for big parts,
  `near()` hints for decoupling, packed regions for the rest). Regenerate after the
  schematic, since pcbgen writes the board rules and netclasses into the .kicad_pro:
  `FREEROUTING_JAR=/path/freerouting-1.9.0.jar python3 -m pcbai.pcbgen $GGSM/hardware/ggsmdriver.py $GGSM/kicad --timeout 1500`
  Use Freerouting 1.9.0 (2.1.0 never stops on this board); it runs headless under
  `xvfb-run` (apt `xvfb`, Java 17+). About 6 min. Output: `kicad/ggsmdriver.kicad_pcb`,
  `kicad/reports/drc.txt`, `pcb-summary.txt`, `preview-*.png`.
  Last run: 0 unrouted, no DRC errors; warnings left are silkscreen overlaps
  (reference text) and 4 dangling fanout stubs. Routing varies a little run to run.
- Gustav's M3 review (2026-10-16): layout is fine as a practice run. He wants to
  rework routing, some placement and the silkscreen **later**; we may come back,
  change `layout()` and regenerate PCB and fab files then.
- Fab files (milestone 4): `python3 -m pcbai.fab $GGSM/hardware/ggsmdriver.py $GGSM/kicad`
  writes `kicad/fab/ggsmdriver-gerbers.zip`, `-bom.csv`, `-cpl.csv` for JLCPCB
  (BOM from the `LCSC` fields in the .py). Rerun after any PCB regeneration.
  CPL rotations are KiCad's; check them in JLC's placement preview.
- Firmware (2026-10-17, in review): `firmware/` is an STM32 HAL port of ggstepper core +
  the Automato bridge (`automato.serial.v2` over USB CDC). `make` in `firmware/` builds
  (arm-none-eabi-gcc 13.2; ~61 KB flash). CubeIDE `.project/.cproject` adapted from ST's
  template, never opened in CubeIDE. Nothing run on hardware yet. Open items are the
  `TODO(verify)` list in `firmware/README.md`. Current: IRUN=IHOLD=3, VSENSE=1 with
  0.05 Ω ≈ 0.23 A RMS; VREF trimmer has no effect (GCONF i_scale_analog=0).
- Files are KiCad 7 format (cloud container has KiCad 7.0.11 via apt; no newer
  KiCad reachable). KiCad 10 opens and upgrades them. KiCad 7's CLI has no ERC:
  the generator checks connectivity via the exported netlist plus a structural
  check; real ERC is run by Gustav in KiCad 10.

- Plan: `PLAN.md`. Circuit spec, pin map and parts: `requirements.md`.
- Reference used for G0B1 + TMC2209 + USB-C: BTT EBB36 CAN V1.2 schematic
  (github.com/bigtreetech/EBB). ST pin data: github.com/STMicroelectronics/STM32_open_pin_data.
- jlcpcb.com and lcsc.com are blocked for curl in the cloud container; WebFetch works.
- Adafruit reference: clone github.com/adafruit/Adafruit-TMC2209-Breakout-PCB.
- Existing firmware to port later: `roaring/automato/middleware/ggstepper/` in
  `gustavgaston/works`.
