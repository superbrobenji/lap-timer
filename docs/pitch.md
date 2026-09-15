# Trackday Lap Timer

*Design pitch, September 2026*

A small battery-powered box that lives on the bike. It recognises the circuit, times every lap and sector, shows how far you leaned and how hard you braked, and hands the whole day to your phone afterwards. No phone on the bike, no signal, no subscription.

## How a track day goes

| When | What happens |
|------|--------------|
| 07:30 | **Mount it and forget it.** It sleeps in the paddock and wakes the moment the bike moves. |
| 09:05 | **It knows where it is.** Killarney, Zwartkops, Kyalami and the other South African circuits are built in, short and reverse layouts included. An unknown track is added with two button presses on your first lap. |
| 09:07 | **Cross the line, glance down.** Each time you pass start/finish the screen shows your last lap, your best, and whether the sector you just rode was up or down on your best. |
| 09:30 | **Back in the pits it naps.** The screen keeps its last numbers without using any power. A tap on the bike wakes it for the next session. |
| 16:00 | **Pull the day to your phone.** Open a page in your browser, connect over Bluetooth, and the sessions land in RaceChrono, ready for video overlays and comparisons. |
| Later | **Charge it when you remember.** Two ordinary 18650 cells give around twenty track days between charges. |

## What the rider sees

The display is e-paper, the same kind as an e-reader. It is perfectly readable in full sun, draws no power while it holds an image, and only changes when something worth reading happens: a sector, a lap, a warning. Nothing flickers or counts up while you ride.

**Lap mode.** Best lap, previous lap, time so far at the last sector gate, and the delta for the sector just completed. Negative means faster than your best.

```
BEST   1:51.90
PREV   1:52.34
CUR    1:12.30   S2
ΔS     -0.21
```

**Drag mode.** Rows appear as you reach each speed. The quarter mile shows elapsed time and the speed through the finish, measured the way drag strips do it.

```
0-100     5.91
0-200    12.40
0-300    19.87
1/4     14.20 @ 305
```

Three big buttons, usable in gloves, switch modes and pages. A menu is only reachable when the bike is stationary.

## What it records

- Lap and sector times, best lap, and the theoretical best made of your best sectors.
- Lean angle left and right, braking and acceleration force, cornering force, top speed, all per lap.
- Drag runs: 0–60, 0–100, 0–200, 100–200, 60 ft, eighth and quarter mile with trap speed, and braking distance from 100.
- The full ride, ten times a second, kept on the device and exported in the formats RaceChrono, Harry's LapTimer and RaceRender already read.

## Built to survive a track day

Track days are hot, wet, and bumpy, and there is no one to reboot a gadget mid-session. So the timer checks its own sensors at every start, recovers a stuck sensor on its own, and if it ever restarts mid-lap it picks the lap up where it was and marks it. Results are written in a way that survives a flat battery or a yanked cable. Software updates arrive over Bluetooth and roll back by themselves if an update misbehaves.

## Honest about accuracy

Every GPS lap timer on the market has the same limit: the satellite fix is good to a metre or two, and at track speeds a metre is a few hundredths of a second. What sellers print as "1/1000 second" is the display format, not the measurement.

| Configuration | Lap-to-lap repeatability | Comparable to |
|---------------|--------------------------|---------------|
| Prototype, 5 Hz GPS | about ±0.03–0.05 s | older consumer timers |
| Upgraded 10 Hz GPS (a drop-in module) | about ±0.01–0.02 s | SpeedAngle, AiM Solo, RaceChrono on a good receiver |

## Where it is

The design is written down in full. The device itself is not built yet. The first building blocks of the software, the timing maths, track recognition and data export, are being written and tested on a desktop computer before anything runs on the board. Batteries arrive next week; the GPS and motion sensor in about a month; the display is still to be ordered. First real track session follows once those are in hand.

**After the motorcycle version:** a car version with a bright OLED screen and live "faster or slower" readout, Wi-Fi download without an app, live streaming to RaceChrono on the phone, and the better GPS as a plug-in upgrade.
