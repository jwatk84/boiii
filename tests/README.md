# Dedicated vehicle regression test

`Test-DedicatedVehiclePathLayout.ps1` guards the ABI layout used by the
dedicated Origins/vehicle hooks. It compiles the overlay and matrix function
directly from `src/client/component/dedicated_vehicle.cpp`, then checks them
against an independent byte fixture.

The fixture is based on Ezz-lol/boiii-free commit
`fefeba3e488d25f4da153fc8044f6fa579ac718a` (with vehicle follow-ups
`937f147d0c7a15f5b86bf022c0628d4a2d1d2c91` and
`1d64404bedb64c325ad5f1c43d340145702b7762`). The source definition and Ezz
Release PDB place `vehicle_pathpos_t::origin` at `0x28`, `angles` at `0x34`,
and the structure size at `0x164`. Reading either vector four bytes late
scrambles both tank translation and rotation.

Run from the repository root in PowerShell:

```powershell
.\tests\Test-DedicatedVehiclePathLayout.ps1
```

The script discovers Visual Studio C++ Build Tools with `vswhere`. If Visual
Studio is installed in a non-standard location, pass its developer command
prompt script explicitly:

```powershell
.\tests\Test-DedicatedVehiclePathLayout.ps1 `
  -VsDevCmdPath 'D:\BuildTools\Common7\Tools\VsDevCmd.bat'
```
