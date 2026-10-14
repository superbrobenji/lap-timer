# Display glyphs — what each icon in the status strip means

The riding screens carry a strip of 12×12 icons along the bottom-right edge (spec §20.5: the
fault-icon strip; on the 2.13" panel it sits at the bottom right, drawn right-to-left from the
corner). An icon is only drawn while its condition holds, so an empty strip means "nothing to
report". The bitmaps live in `components/core/ui/icons.c`; which condition draws which icon is the
table `FAULT_ICON_FOR_BIT` in `components/core/ui/screens_moto.c`.

| Glyph | Looks like | Means | What to do |
|---|---|---|---|
| GPS strike | hollow GPS ring with a diagonal strike | The GPS module is not answering at all (`SYS_GPS_DEAD`). Nothing is being timed. | Check the module/wiring (BOOT shows `GPS FAIL`). |
| GPS hollow | hollow location pin | No fix yet (`SYS_GPS_NOFIX`): the receiver is searching. Nothing is timed until it clears. | Wait outdoors with a clear sky view. |
| IMU strike | `?` with a diagonal strike | The IMU is not answering (`SYS_IMU_DEAD`): no lean, no lateral g, drag launches fall back to GPS only. | Check the I²C wiring (BOOT shows `IMU FAIL`). |
| IMU question mark | `?` | The orientation/fusion quality disagrees with itself (`SYS_IMU_SUSPECT`): lean and g values are unreliable. | Re-mount or recalibrate (Calibrate menu — see issue #93 until it is wired). |
| Disk strike | floppy outline with a diagonal strike | Storage is dead / not mounted (`SYS_STORAGE_DEAD`): nothing is being logged. | Reboot; if it persists the filesystem needs reformatting. |
| Disk, filled | floppy disk with solid panels | Storage is full (`SYS_STORAGE_FULL`): old sessions are being evicted to make room. | Download and delete sessions from the dev-kit. |
| Disk with `!` | floppy disk with an exclamation mark | Storage is degraded (`SYS_STORAGE_DEGRADED`): a write failed or the filesystem is near its limit. | Download sessions soon; watch the errlog. |
| Battery + `%NN` | battery outline with one low segment, a `%<pct>` label to its left | Battery low (`SYS_BATT_LOW`); the label is the remaining percentage. | Charge. An OTA update is refused below the §19.5 threshold. |
| Padlock | padlock | Safe mode (`SYS_SAFE_MODE`): the device crashed three times in a row, so sample logging, BLE and WiFi are off; a crash loop inside safe mode escalates to recovery mode (console only, no screen). | Read the errlog on the dev-kit; `dbg safe clear` on the lap-timer console clears the gate, an OTA update also clears it. |
| Thermometer | stem + bulb | The e-paper is temperature-throttled (`SYS_DISP_TEMP_THROTTLE`): full refreshes are suppressed, partials limited to one per 30 s, so the screen updates slowly and may ghost. | Let the display warm up / cool down; it clears on its own. (The producer is not wired yet — issue #82.) |
| Lean `?` | `?` beside a leaning mast on a ground line | Fusion disagrees with itself (`SYS_FUSION_DISAGREE`): the lean angle shown is not trustworthy. | Re-run Calibrate (#93 until wired); check the mount. |
| `S` | bold S | Simulated inputs: this firmware is a sim build (`moto_sim`), its GPS and IMU are fake. Always on in a sim build. | Never ride with this image. |
| `>>` | double chevron | Moving: the menu is motion-locked (speed above the lock threshold, §20.7). On the bench it is the "sim run in progress" cue — it disappears once the sim parks, and the menu opens again. | Stop (or wait for the sim to park) before pressing MODE for the menu. |
| Chain links | two linked loops | LINK: the dev-kit (or a BLE peer) is connected; disappears a few seconds after the link goes quiet. | Informational only — plug in the dev-kit (or connect a BLE peer) to see it. |

Conditions with no icon (they appear in the errlog / dev-kit instead): `SYS_DISP_DEAD` (the display
itself is gone, so there is nowhere to draw it), `SYS_HEAP_LOW` and `SYS_OTA_PENDING` (an update is
on trial for its first 30 s). Every other §17.4 bit now has its own glyph. The healthy-storage and
Bluetooth bitmaps were deleted: no condition ever drew them (`sys_flags` has no "storage OK" bit,
and BLE is sub-project C), and a bitmap nothing draws is dead flash.

One-shot screens are not glyphs but share the "what does this mean" question: `BOOT` (name,
version, four self-test lines STORAGE / DISPLAY / GPS / IMU, each `OK`, `FAIL` or `SIM`), `VENUE`
(venue then layout once locked), `SAFE MODE`, `LOW BATT`, `UPDATING` (OTA in progress: bar, percent
and RECEIVING / VERIFYING / REBOOTING — buttons are ignored until it finishes; it gives up after
60 s without progress), `UPDATE FAILED / REVERTED` (the bootloader rolled a bad update back; shown
3 s after BOOT), `CALIBRATE`, `NEW TRACK`.
