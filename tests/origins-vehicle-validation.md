# Origins tank path-layout correction

Validated on 25 September 2026. This record supersedes the original PR's claim
that every path-position field had been independently checked.

## Defect and correction

The initial port read `vehicle_pathpos_t::origin` at `0x2C` and `angles` at
`0x38`. The public Ezz source and its compiled Release debug type information
place them at `0x28` and `0x34`, respectively. The structure remains `0x164`
bytes long. The incorrect overlay mixed coordinates into Euler angles and
produced a tank that twisted, floated into the church ceiling, and killed a
rider. The correction changes only these field offsets and compensating
padding; it does not alter gravity, path nodes, vehicle speed, or map scripts.

Provenance: Ezz-lol/boiii-free `fefeba3e488d25f4da153fc8044f6fa579ac718a`, with
the vehicle follow-ups already listed in the component. Field verification
used the public source pinned at `1b79eb9b644044437f1852f9d43b3e633cce8388`.

## Evidence

- The byte-layout regression compiles the actual production overlay and
  matrix function against independently positioned fixture bytes. It fails
  on original commit `0b6e35f4b77bd6810981b415a0a3610171ac072b` and passes after
  this correction. The fixture also checks the expected rotation and
  translation matrix, with sentinel values in the adjacent look-position
  field to expose shifted reads.
- Corrected standalone feature source: MSVC Release x64 build passed.
- Corrected integration source: MSVC Release x64 build passed and was deployed
  to an isolated Wine 11.0 dedicated server with a real client connected.
- Tested integration executable SHA256:
  `A9AA7754681C358D8FBEB3D76A04BEE20AC34FA7C574CD27D22C7D6320819C3A`.
- Dedicated game executable SHA256:
  `3DF511D71E3690B14517379783BBFB713F6FE2ED2F2E73B445B4878216686156`.
- The tester confirmed the tank was upright and following the track, then
  reported normal operation while providing an on-tank gameplay screenshot.
- Passive server transform logging sampled at 0.1 seconds and recorded
  every second or on a rotation alert. Across the first 301 regular samples,
  pitch ranged from -19.27 to 14.64 degrees, roll from -6.24 to 5.60 degrees,
  and reported path speed reached 8 mph. Angle thresholds are diagnostics,
  not a complete physics correctness oracle.
- The only recorded angular alert in that capture was a yaw change during
  initial tank setup at 100 ms, before the player joined; no later alert was
  recorded in those first 301 regular samples.
- Remaining accessed overlay fields, initialized-flag bit, function
  signatures/addresses, and matrix formula were rechecked against Ezz.
- `git diff --check` passed.

## Scope and limitations

The runtime-tested integration build also included the separate sound patch
and local-only client-connection compatibility and cheat support. Those
changes, the 50,000-point spawn script, and the passive logger are **not** part
of this feature branch. The corrected standalone feature binary was built
but was not the binary used for this real-client acceptance test.

The previous headless two-leg probe checked distance, path speed, stops, and
station changes; it did not establish correct physical orientation or rider
behavior. Its successful completion was insufficient acceptance coverage.
This correction has real-client evidence, but is not a multi-hour soak test
or exhaustive coverage of all vehicle-using custom maps. The corrected
revision has not been retested under Debug or ClangCL.

## Rollback

No persistent data migration is involved. Redeploy the previously accepted
launcher to disable the vehicle feature, or revert the feature as a whole.
Reverting only this offset correction restores the known broken transform
and is not a safe runtime fallback. Do not deploy the original PR1 head alone.
