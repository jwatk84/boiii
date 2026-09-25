param(
	[string]$SourcePath = (Join-Path $PSScriptRoot '..\src\client\component\dedicated_vehicle.cpp'),
	[string]$VsDevCmdPath
)

$ErrorActionPreference = 'Stop'

function Find-VsDevCmd
{
	if ($VsDevCmdPath)
	{
		return (Resolve-Path -LiteralPath $VsDevCmdPath).Path
	}

	$vswhereCandidates = [System.Collections.Generic.List[string]]::new()
	$pathVswhere = Get-Command 'vswhere.exe' -ErrorAction SilentlyContinue
	if ($pathVswhere)
	{
		$vswhereCandidates.Add($pathVswhere.Source)
	}

	$programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
	if ($programFilesX86)
	{
		$vswhereCandidates.Add((Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'))
	}

	foreach ($vswhere in ($vswhereCandidates | Select-Object -Unique))
	{
		if (-not (Test-Path -LiteralPath $vswhere))
		{
			continue
		}

		$installationPath = & $vswhere -latest -products '*' `
			-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
			-property installationPath
		if ($LASTEXITCODE -eq 0 -and $installationPath)
		{
			$candidate = Join-Path ($installationPath | Select-Object -First 1) 'Common7\Tools\VsDevCmd.bat'
			if (Test-Path -LiteralPath $candidate)
			{
				return (Resolve-Path -LiteralPath $candidate).Path
			}
		}
	}

	throw 'Visual Studio C++ Build Tools were not found. Install the x64 C++ tools or pass -VsDevCmdPath.'
}

function Get-ProductionBlock
{
	param(
		[Parameter(Mandatory)]
		[string]$ProductionSource,

		[Parameter(Mandatory)]
		[string]$Pattern,

		[Parameter(Mandatory)]
		[string]$Description
	)

	$match = [regex]::Match($ProductionSource, $Pattern,
		[System.Text.RegularExpressions.RegexOptions]::Multiline -bor
		[System.Text.RegularExpressions.RegexOptions]::Singleline)
	if (-not $match.Success)
	{
		throw "Could not extract $Description from $resolvedSource"
	}

	return $match.Value
}

$resolvedSource = (Resolve-Path -LiteralPath $SourcePath).Path
$productionSource = Get-Content -LiteralPath $resolvedSource -Raw
$resolvedVsDevCmd = Find-VsDevCmd

# Compile the production definitions instead of maintaining a duplicate test copy.
# The fixture remains independent: its offsets are pinned to Ezz's documented ABI.
$vec3 = Get-ProductionBlock $productionSource '^\s*struct vec3\s*\{.*?^\s*\};' 'vec3'
$vec4 = Get-ProductionBlock $productionSource '^\s*struct vec4\s*\{.*?^\s*\};' 'vec4'
$matrix43 = Get-ProductionBlock $productionSource '^\s*struct matrix43\s*\{.*?^\s*\};' 'matrix43'
$pathPosition = Get-ProductionBlock $productionSource '^\s*struct vehicle_path_position\s*\{.*?^\s*\};' 'vehicle_path_position'
$makePathMatrix = Get-ProductionBlock $productionSource '^\t\tmatrix43 make_path_matrix\s*\(.*?^\t\t\}\r?$' 'make_path_matrix'

$harness = @"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

$vec3
$vec4
$matrix43

#pragma pack(push, 1)
$pathPosition
#pragma pack(pop)

$makePathMatrix

namespace fixture
{
	// Independent oracle from Ezz fefeba3e48 vehicle_pathpos_t and its PDB layout.
	constexpr std::size_t path_position_size = 0x164;
	constexpr std::size_t origin_offset = 0x28;
	constexpr std::size_t angles_offset = 0x34;
	constexpr std::size_t look_position_offset = 0x40;

	constexpr vec3 expected_origin{123.25f, -456.5f, 789.75f};
	constexpr vec3 expected_angles{0.0f, 90.0f, 0.0f};
	constexpr vec3 poison_look_position{12345.0f, -23456.0f, 34567.0f};
}

bool near(const float lhs, const float rhs)
{
	return std::fabs(lhs - rhs) <= 0.0001f;
}

bool equal(const vec3 &lhs, const vec3 &rhs)
{
	return near(lhs.x, rhs.x) && near(lhs.y, rhs.y) && near(lhs.z, rhs.z);
}

int main()
{
	std::array<std::byte, fixture::path_position_size> bytes{};
	std::memcpy(bytes.data() + fixture::origin_offset, &fixture::expected_origin,
		sizeof(fixture::expected_origin));
	std::memcpy(bytes.data() + fixture::angles_offset, &fixture::expected_angles,
		sizeof(fixture::expected_angles));
	std::memcpy(bytes.data() + fixture::look_position_offset, &fixture::poison_look_position,
		sizeof(fixture::poison_look_position));

	vehicle_path_position production_overlay{};
	if (sizeof(production_overlay) != bytes.size())
	{
		std::fprintf(stderr, "FAIL: production overlay size is 0x%zX; expected 0x%zX\n",
			sizeof(production_overlay), bytes.size());
		return 1;
	}
	std::memcpy(&production_overlay, bytes.data(), bytes.size());

	if (!equal(production_overlay.origin, fixture::expected_origin) ||
		!equal(production_overlay.angles, fixture::expected_angles))
	{
		std::fprintf(stderr,
			"FAIL: overlay mapped origin=(%.2f, %.2f, %.2f), angles=(%.2f, %.2f, %.2f); "
			"expected origin=(%.2f, %.2f, %.2f), angles=(%.2f, %.2f, %.2f)\n",
			production_overlay.origin.x, production_overlay.origin.y, production_overlay.origin.z,
			production_overlay.angles.x, production_overlay.angles.y, production_overlay.angles.z,
			fixture::expected_origin.x, fixture::expected_origin.y, fixture::expected_origin.z,
			fixture::expected_angles.x, fixture::expected_angles.y, fixture::expected_angles.z);
		return 1;
	}

	const matrix43 matrix = make_path_matrix(production_overlay.angles, production_overlay.origin);
	const bool matrix_matches =
		near(matrix.x.x, 0.0f) && near(matrix.x.y, 1.0f) && near(matrix.x.z, 0.0f) && near(matrix.x.w, 0.0f) &&
		near(matrix.y.x, -1.0f) && near(matrix.y.y, 0.0f) && near(matrix.y.z, 0.0f) && near(matrix.y.w, 0.0f) &&
		near(matrix.z.x, 0.0f) && near(matrix.z.y, 0.0f) && near(matrix.z.z, 1.0f) && near(matrix.z.w, 0.0f) &&
		near(matrix.position.x, fixture::expected_origin.x) &&
		near(matrix.position.y, fixture::expected_origin.y) &&
		near(matrix.position.z, fixture::expected_origin.z) && near(matrix.position.w, 1.0f);
	if (!matrix_matches)
	{
		std::fprintf(stderr, "FAIL: production path matrix does not match the independent fixture oracle\n");
		return 1;
	}

	std::printf("PASS: production overlay maps Ezz offsets 0x28/0x34 and builds the expected path matrix\n");
	return 0;
}
"@

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("boiii-vehicle-layout-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null

try
{
	$harnessPath = Join-Path $temporaryDirectory 'vehicle_path_layout_test.cpp'
	$binaryPath = Join-Path $temporaryDirectory 'vehicle_path_layout_test.exe'
	$objectPath = Join-Path $temporaryDirectory 'vehicle_path_layout_test.obj'
	[System.IO.File]::WriteAllText($harnessPath, $harness, [System.Text.UTF8Encoding]::new($false))

	$compileCommand = "call `"$resolvedVsDevCmd`" -no_logo -arch=amd64 -host_arch=amd64 >nul && cl.exe /nologo /std:c++20 /EHsc /W4 /WX `"$harnessPath`" /Fo:`"$objectPath`" /Fe:`"$binaryPath`""
	& $env:ComSpec /d /s /c $compileCommand
	if ($LASTEXITCODE -ne 0)
	{
		throw "Regression harness compilation failed with exit code $LASTEXITCODE"
	}

	& $binaryPath
	if ($LASTEXITCODE -ne 0)
	{
		throw "Vehicle path layout regression test failed with exit code $LASTEXITCODE"
	}
}
finally
{
	if (Test-Path -LiteralPath $temporaryDirectory)
	{
		$resolvedTemporaryDirectory = (Resolve-Path -LiteralPath $temporaryDirectory).Path
		$temporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
		$temporaryLeaf = Split-Path -Leaf $resolvedTemporaryDirectory
		if (-not $resolvedTemporaryDirectory.StartsWith($temporaryRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
			-not $temporaryLeaf.StartsWith('boiii-vehicle-layout-', [System.StringComparison]::Ordinal))
		{
			throw "Refusing to remove unexpected temporary path: $resolvedTemporaryDirectory"
		}

		Remove-Item -LiteralPath $resolvedTemporaryDirectory -Recurse -Force
	}
}
