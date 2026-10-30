# Track research: Killarney Full / Full Reverse, Kyalami GP (2026-10-29, investigator, read-only)

Scratch evidence: `/private/tmp/claude-501/-Users-~-projects-personal-lap-timer/b1ec9f20-b80f-4fda-abcf-bfd5962cee25/scratchpad/tracks/`
(OSM dumps `dl/osm2.json` (Kyalami) and `dl/osm3.json` (Killarney), `dl/way_*.json` (OSM API), imagery tiles and crops `img/`, scripts `geo.py derive.py xcheck.py`, proposed JSON `proposed/`).

Method notes
- OSM = OpenStreetMap via Overpass (osm3s timestamp 2026-10-29T18:19Z) and api.openstreetmap.org `way/<id>/full.json`.
- IMG = Esri World Imagery tiles, zoom 20 (0.124 m/px Killarney, 0.134 m/px Kyalami); a painted line was read off the pixel grid and converted with the Web-Mercator tile formula (`tiles.py`). Reading error about +-10 px = +-1.3 m, plus unknown imagery georeferencing error.
- IMG and OSM are separate sources (Killarney OSM was traced from "Kaapstad 2015 luchtfoto" per its `source` tag). Check: imaged S/F midpoint to OSM centreline: Killarney 1.3 m, Kyalami 0.2 m.
- Lengths from OSM are the summed great-circle length of the centreline polyline (R = 6371008.8 m, the same as `gen_tracks.dist_m`).
- Direction from geometry is the sign of the shoelace area in ENU: negative = clockwise.
- No lap-timer vendor database could be read. Lapmeta returns Cloudflare 403 to both WebFetch and curl. Searches found no RaceBox, RaceChrono, AiM, VBOX or Harry's entry. Everything below therefore comes from OSM + imagery + operator/club documents + Wikipedia.

## 1. Killarney Full (Killarney International Raceway, Table View, Cape Town)

| field | value | sources | agreement | confidence |
|---|---|---|---|---|
| venue centre | -33.82425, 18.52888 (bbox centre of the OSM circuit way 42125321) | [K1] OSM; [K2] Wikipedia coord 33°49'24"S 18°31'40"E = -33.82333, 18.52778 | 144 m apart; both inside the circuit bbox | high |
| radius | 2000 m (max distance from centre to the circuit = 498 m) | DERIVED from [K1] | n/a | high |
| lap length | 3267 m | [K2] "3.267 km (2.030 mi)"; [K3] racingcircuits.info "2.030 miles / 3.267 km"; [K4] WPMC SR "3.267km long"; [K1] OSM polyline 3268.1 m | 1.1 m | high |
| direction | clockwise | [K4] WPMC Powerseries SRs 30 May 2026: "The circuit is 3.267km long and the race direction is clockwise." (font-encoded PDF, decoded by a 64-char one-to-one substitution with no conflicts); [K5] Wikipedia SVG `Killarney_Motor_Racing_Circuit.svg`: red arrow north on the main straight, turns 1..9 in OSM node order; [K1] OSM node order matches the SVG turn sequence, shoelace area -127530 m² (clockwise); [K8] carmag: reversed "Killarney has essentially become a counter-clockwise circuit"; Lapmeta (search snippet only, page 403) "clockwise" | all agree | high |
| S/F line, edge to edge | p1 (left/west) -33.8237104, 18.5290291; p2 (right/east, pit wall) -33.8237285, 18.5291431; width 10.7 m | [K6] IMG: full-width white line next to the timing box at the pit wall (crop `img/sf_crop.png`, pixels (437.5,563.75)-(522.5,580) of `img/sf_z20.png`, origin tile z20 x578256 y629082); [K5] SVG chequer at ~-33.8238 (+-22 m, from a linear fit of the SVG to OSM extremes); [K1] OSM node 127 (-33.8238187, 18.5290773), where the pit drive-through way 577168386 rejoins | IMG vs OSM node 127: 12 m along track; IMG vs SVG: within the SVG's +-22 m | MEDIUM. A chequered band 17.4 m further north (north edge p1 -33.8235558, 18.5290576 / p2 -33.8235739, 18.5291766) could be the line instead. **Needs on-track marking.** |
| sector lines | none official found | searched WPMC SRs, racingcircuits.info, Wikipedia: no sector definitions | n/a | UNKNOWN |
| proposed sector 1 (DERIVED) | centre -33.8215874, 18.5271511 (track heading 5°, leg between Engen and Kink), at 36.4 % of the lap | DERIVED: perpendicular to the OSM centreline, +-15 m; moved from 33 % so that it crosses no rallycross or spur way | not a source value | owner choice |
| proposed sector 2 (DERIVED) | centre about -33.82464, 18.52963 (back straight, heading 206°), at 66.7 % of the lap | DERIVED as above | not a source value | owner choice |
| pit lanes | two, on either side of the main straight | [K2] "two pit lanes on the home straight"; [K3] "two pit lane complexes on either side of the main straight" | agree | high |

## 2. Killarney Full Reverse

| field | value | sources | agreement | confidence |
|---|---|---|---|---|
| exists | yes, run occasionally (2015/16, and Aug 2026 Power Series) | [K7] athlonenews 2026-08-24: "raced in reverse for the first time in 10 years"; [K8] carmag Project MX-5 part 10: "run the circuit in a reverse direction" | agree | high |
| direction | anticlockwise | [K8] "essentially become a counter-clockwise circuit"; [K4] forward = clockwise | agree | high |
| S/F line | UNKNOWN whether the same line is used. The generator's `"sf": "same"` assumes the same physical line, crossed the other way | [K7] and [K8] do not mention the S/F or timing line | n/a | UNKNOWN |
| layout differences | [K8]: "creating a new chicane part way down the front straight", leading into "Cape Town corner, i.e. the new T1"; the chicane was "makeshift" | [K8] SINGLE-SOURCE (2016) | n/a | medium |
| lap length | UNKNOWN. 3267 m assumed (a temporary chicane changes the driven distance by an unknown amount) | none | n/a | UNKNOWN |

## 3. Kyalami Grand Prix Circuit (current layout, 2015 to now, Midrand)

| field | value | sources | agreement | confidence |
|---|---|---|---|---|
| venue centre | -25.99688, 28.06764 (bbox centre of the 24 chained OSM GP ways) | [Y1] OSM; [Y2] Wikipedia 25°59'53"S 28°4'08"E = -25.99806, 28.06889; [Y3] official site map link ll=-25.996081, 28.074441 | Wikipedia 181 m away; the official-site point is 686 m away (likely the gate or entrance) | high |
| radius | 2000 m (max distance from centre to the circuit = 821 m) | DERIVED from [Y1] | n/a | high |
| lap length | 4522 m (official). Wikipedia says 4529 m; OSM polyline 4525.0 m | [Y3] "Length (m): 4.522km"; [Y2] "4.529 km (2.814 mi)"; [Y1] | 7 m spread | high (value choice is an owner question) |
| direction | anticlockwise | [Y3] "Track run/direction: Anticlockwise"; [Y1] OSM ways are oneway=yes, chained order Kink, Crowthorne, Jukskei Sweep, Barbeque, Sunset, Clubhouse, Esses, Leeukop, Mineshaft, Crocodiles, Cheetah, Ingwe; shoelace area +295061 m² (anticlockwise); [Y3] "16 turns (6) Right, (10) Left" fits an anticlockwise lap | agree | high |
| S/F line, edge to edge | p1 (left/NW, pit side) -25.9987236, 28.0698555; p2 (right/SE) -25.9988120, 28.0699674; width 14.9 m | [Y4] IMG: full-width white line at the head of the grid (crop `img/ky_crop.png`, pixels (353.3,461.7)-(436.7,535) of `img/ky_sf.png`, origin tile z20 x606046 y602754); [Y1] OSM node 2331688961 `raceway=start-finish` at -25.998677, 28.0700067 | OSM node is 13.8 m downstream of the imaged line (near the start-light gantry), 8 m along the line from p1. Track width 14.9 m vs official "average 12 m" | MEDIUM. The painted line may be the start line rather than the timing line. **Needs on-track marking.** |
| sector lines | none official found | no SRO/Kyalami 9h or MSA sector map found | n/a | UNKNOWN |
| proposed sectors (DERIVED) | S1 centre -25.99658, 28.06938 (Sunset, heading 217°, 33 %); S2 centre -26.00046, 28.06148 (after Leeukop, heading 66°, 67 %) | DERIVED, perpendicular to the OSM centreline, +-15 m, crossing no other way | not a source value | owner choice |
| pole side | left | [Y3] "Pole Positions: Left side" | IMG grid stagger consistent | high |

Sources
- [K1] OSM way 42125321 "Killarney Race Track" v16 (2023-10-11), 138 nodes, closed; Overpass dump `dl/osm3.json`.
- [K2] https://en.wikipedia.org/wiki/Killarney_Motor_Racing_Complex (infobox, "Full Circuit (1960–present) 3.267 km").
- [K3] https://www.racingcircuits.info/africa/south-africa/killarney.html
- [K4] https://www.motorsport.co.za/wp-content/uploads/2026/05/WPMC-Powerseries-SRs-30-May-2026.pdf (decoded sentence quoted above; decode check in report).
- [K5] https://commons.wikimedia.org/wiki/File:Killarney_Motor_Racing_Circuit.svg (rendered `dl2/kl.svg.png`; labels 1 WesBank Corner, 2 Engen, 3 Kink, 4 Shell Sweep, 5, 6 Malmesbury, 7 Back Straight, 8 Cape Town Corner, 9 Chicane).
- [K6] Esri World Imagery z20 tiles (server.arcgisonline.com World_Imagery), `img/sf_z20.png`.
- [K7] https://athlonenews.co.za/table-talk/sport/2026-08-24-power-series-at-killarney-round-6-sees-drivers-tackle-a-reverse-layout/
- [K8] https://www.carmag.co.za/speed/project-cars/project-mx-5-part-10-race-weekend-4-wrong-way/
- [Y1] OSM ways 532871442 and 1359379863..1359379885 (Kyalami Grand Prix Circuit, oneway=yes) and node 2331688961 raceway=start-finish (way v6, 2025-02-12); `dl/osm2.json`, `dl/way_532871442.json`.
- [Y2] https://en.wikipedia.org/wiki/Kyalami_Grand_Prix_Circuit
- [Y3] https://www.kyalamigrandprixcircuit.com/motoring-venue/
- [Y4] Esri World Imagery z20, `img/ky_sf.png`.

## 4. Comparison with the current `tools/tracks/killarney.json`

The current venue sits about 3.8 km SOUTH of the circuit. Its centre (-33.8567, 18.5170) is 3842 m from the Wikipedia coordinate, and the nearest point of the real circuit is 3273 m from it, outside its own 2000 m radius. The Theo Marius go-kart track (OSM way 702314769) is 1138 m from that centre.

| point | current | proposed | distance |
|---|---|---|---|
| centre | -33.8567, 18.5170 | -33.82425, 18.52888 | 3771 m |
| sf p1 | -33.85780, 18.51520 | -33.8237104, 18.5290291 (edge) | 4000 m |
| sf p2 | -33.85795, 18.51535 | -33.8237285, 18.5291431 (edge) | 4013 m |
| s1 p1 | -33.85300, 18.51900 | -33.8215749, 18.5269896 | 3571 m |
| s1 p2 | -33.85315, 18.51925 | -33.8215999, 18.5273126 | 3586 m |
| s2 p1 | -33.85950, 18.52100 | -33.8246962, 18.5297807 | 3954 m |
| s2 p2 | -33.85965, 18.52080 | -33.8245778, 18.5294888 | 3982 m |

The spec example in `docs/superpowers/specs/2026-09-14-lap-timer-design.md:1458` (`"lat": -33.8567, "lon": 18.5169`, sf -33.8580/18.5150) has the same offset. Length 3267 m and dir +1 (clockwise, with p1 on the left) agree with the sources. The current Short and Short Reverse layouts (2500 m) share the wrong S/F. No source for a 2500 m short layout was found: racingcircuits.info lists "Short circuit: Unknown" length.

## 5. Proposed contents (generator format; validated in scratch)

`python3 -I tools/tracks/gen_tracks.py proposed/killarney.json proposed/kyalami.json tools/tracks/zwartkops.json -o <scratch>/out.c` gave `wrote ...: 3 venues`. The dir sign of every line was checked against the OSM travel direction: +1 for sf, s1 and s2 at both venues (`xcheck.py`).

Lines in the JSON are 30 m (+-15 m, the same as on-device `GATE_HALF_WIDTH_M 15.0`, `components/core/include/core/consts.h:31`). Each is centred on the imaged S/F midpoint along the imaged line's bearing, or for a sector perpendicular to the OSM centreline. The edge-to-edge alternative is in the tables above. Killarney's edge line is 10.7 m, only 0.7 m over the generator's 10 m minimum.

```json
{
  "id": 6, "name": "Killarney", "lat": -33.82425, "lon": 18.52888, "radius_m": 2000, "verified": false,
  "layouts": [
    { "id": 1, "name": "Full", "dir": 1, "length_m": 3267,
      "sf": [[-33.8236941, 18.5289267], [-33.8237448, 18.5292455]],
      "sectors": [ [[-33.8215749, 18.5269896], [-33.8215999, 18.5273126]],
                   [[-33.8246962, 18.5297807], [-33.8245778, 18.5294888]] ] },
    { "id": 2, "name": "Full Reverse", "dir": -1, "length_m": 3267, "sf": "same", "sectors": "reverse" },
    { "id": 3, "name": "Short", "dir": 1, "length_m": 2500,
      "sf": [[-33.8236941, 18.5289267], [-33.8237448, 18.5292455]], "sectors": [] },
    { "id": 4, "name": "Short Reverse", "dir": -1, "length_m": 2500,
      "sf": [[-33.8236941, 18.5289267], [-33.8237448, 18.5292455]], "sectors": [] }
  ]
}
```
(Updated in the follow-up round, after the owner's answers. Validated in scratch with gen_tracks.py: "3 venues".)
Annotations:
- centre/radius = table 1 rows 1-2; length_m = table 1 row 3; dir = table 1 row 4.
- sf = table 1 row 5 (owner Q1: the white line by the timing box), extended to 30 m (owner Q2). It crosses only the circuit way, segment 127 (`derive.py`).
- sectors = table 1 rows 7-8, DERIVED thirds (owner Q4), not official.
- Full Reverse: see section 8. dir -1 = [K9]; `"sf": "same"`, `"sectors": "reverse"` and length_m 3267 are ASSUMPTIONS, because no source gives the reverse S/F, chicane or length.
- Short / Short Reverse (owner Q3: kept, unverified): length_m 2500 is UNSOURCED (racingcircuits.info lists the Short circuit length as "Unknown"). Their sf reuses the Full line, as the current JSON already does: an ASSUMPTION, no source. Sectors are [] because the old sector line is 3.6 km off the venue and no source exists.

```json
{
  "id": 1, "name": "Kyalami", "lat": -25.99688, "lon": 28.06764, "radius_m": 2000, "verified": false,
  "layouts": [
    { "id": 1, "name": "GP", "dir": 1, "length_m": 4522,
      "sf": [[-25.9986786, 28.0697985], [-25.998857, 28.0700244]],
      "sectors": [ [[-25.9966628, 28.0694958], [-25.996498, 28.0692576]],
                   [[-26.0003319, 28.0614154], [-26.0005795, 28.0615379]] ] }
  ]
}
```
Annotations: id 1 is inferred from spec :1417, whose initial-set order puts Kyalami first. That order matches the existing ids Zwartkops = 2 and Killarney = 6. It is still an owner question (Q5). centre/radius = table 3 rows 1-2. length_m = table 3 row 3 (official value). dir +1 = p1 on the left when driving NE on the main straight (table 3 row 4). sf = table 3 row 5, extended to 30 m; that line also crosses the "Grand Prix Pit Lane" way 793806972. sectors = table 3 row 7 (DERIVED).

## 6. Open questions for the owner
1. S/F choice. Killarney: the white line by the timing box (proposed), or the chequered band 17.4 m north? Kyalami: the painted grid-head line (proposed), or the OSM `start-finish` node 13.8 m downstream? Either choice gives correct lap times; only the lap-start point and the comparability of split times change.
2. Line width: 30 m gates (proposed; same as on-device) or edge-to-edge (Killarney 10.7 m, Kyalami 14.9 m, close to the 10 m validator floor and smaller than GPS error margins)?
3. Killarney Short / Short Reverse: drop them, keep them with unknown data, or research them (the length 2500 m is unsourced)?
4. Sectors: no official sector lines were found. Use the derived thirds (proposed), find the ZA Timing / MSA sector points, or ship with no sectors?
5. Kyalami: venue id (1 proposed), layout name ("GP"), and length 4522 (official) vs 4529 (Wikipedia)?
6. Killarney Full Reverse: confirm on the day whether the same S/F line is used and how the temporary front-straight chicane changes the length.

## 7. What most needs on-device confirmation (in order)
1. Both S/F lines: position along the track (the alternatives are 12-17 m apart) and the imagery georeference (unknown, typically a few metres).
2. Killarney Full Reverse: whether the S/F line is the same and what the lap length is.
3. Sector lines: derived only, so mark them on track if official splits matter.
4. Centre, radius, length and direction are well supported and do not need it.

## 8. Killarney Full Reverse (follow-up, 2026-10-29)

The owner ruled that Full Reverse must be the real reverse layout. Findings:

| field | value | sources | agreement | confidence |
|---|---|---|---|---|
| direction | anticlockwise (dir -1 on the Full S/F endpoints) | [K9] WPMC "Power Series Rnd 7 - 22 August" SRs, permit MSA-2026240, issued 2026-08-07, page 11 section 32: "RACE DIRECTION OF THE EVENT WILL BE IN ANTI-CLOCKWISE"; [K7] athlonenews "raced in reverse"; [K8] carmag "essentially become a counter-clockwise circuit" | agree | high |
| temporary chicane position | UNKNOWN. [K8] (2016): "creating a new chicane part way down the front straight", leading into "Cape Town corner, i.e. the new T1". A search-result summary of an IOL/motoring.co.za Cape Superbikes preview says a "disused motorcycle chicane is re-inserted to keep riders away from the drag-strip wall"; the page could not be fetched (HTTP 525 / 403), so the sentence is UNVERIFIED and is not used | [K8] SINGLE-SOURCE, no coordinates | none | UNKNOWN: no coordinates for either end |
| chicane in the 2026 event | UNKNOWN. [K9] says nothing about a chicane, the circuit length or a track map. [K7] says nothing about it either | -- | -- | UNKNOWN |
| lap length | UNKNOWN. 3267 m is used as an approximation. The engine uses length_m only as a +-15 % scoring hint (spec line 1503), so the unknown chicane effect, likely tens of metres, stays well inside 490 m | none | -- | UNKNOWN (assumption) |
| S/F / timing line | UNKNOWN whether the reverse uses the same line. Timekeeper is ZA Timing with transponders ([K9] section 26: "ZA Timing", "Timing transponders"), but the SRs do not place the loop. The Full line is used as an ASSUMPTION, crossed in the opposite direction | [K9] silent on location | -- | UNKNOWN |
| sector splits | none found (official or common) | [K9], [K7], [K8] silent | -- | UNKNOWN |

Searched and found nothing usable:
- the motorsport.co.za WordPress media library (`wp-json/wp/v2/media`) for "Powerseries", "final instructions", "bulletin", "track map", "chicane", "circuit map", "anti-clockwise" and "Killarney map": only the SR PDFs, with no map, final instructions or bulletin for Rnd 7;
- [K9] all 11 pages (scanned images, rendered with PDFKit in `r2/aug-*.png`), which carry no track map and no PDF link annotations;
- the "View Programme File" and "View Notice Board File" entries on [K9] page 7, which are not live links in the PDF;
- satellite imagery: Esri tiles of the south end of the main straight (`img/south2_z19.png`). The capture date is unknown and a temporary cone or tyre chicane would only show if the image was taken on an event day, so imagery cannot place the chicane.

Proposed Full Reverse (in the section 5 JSON):
`{ "id": 2, "name": "Full Reverse", "dir": -1, "length_m": 3267, "sf": "same", "sectors": "reverse" }`
- dir -1: [K9] + [K7] + [K8]. Two independent sources, high confidence.
- sf "same": ASSUMPTION. NEEDS ON-TRACK MARKING at a reverse event, or the ZA Timing loop position from the club.
- sectors "reverse": the DERIVED Full sector lines in reverse order. This is valid for any reverse path that uses the same carriageway; the chicane does not cross either line, because both sit away from the front straight (S1 on the Engen-Kink leg, S2 on the back straight).
- length_m 3267: ASSUMPTION. NEEDS ON-TRACK measurement (the device computes it from integrated distance).
- Chicane ends: UNKNOWN. They are not needed for line crossing unless the chicane cuts across the S/F line, which is unknown. Mark both ends on track or get the club's reverse track map.

To close the gaps: WPMC (WPMC contact (their website), per [K9] page 1) or ZA Timing (per [K9] section 26) can give the reverse track map, chicane position and timing-loop position. These are owner actions; no request was sent.

[K9] https://www.motorsport.co.za/wp-content/uploads/2026/08/WPMC-Powerseries-SRs-22-August-2026.pdf (downloaded to `r2/`; page images `r2/aug-1.png` .. `aug-11.png`)
