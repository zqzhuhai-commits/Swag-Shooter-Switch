param([string]$DevkitPro = 'C:\devkitPro')
$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot
$compiler = Join-Path $DevkitPro 'devkitA64\bin\aarch64-none-elf-gcc.exe'
$make = Join-Path $DevkitPro 'msys2\usr\bin\make.exe'
if (!(Test-Path -LiteralPath $compiler) -or !(Test-Path -LiteralPath $make)) {
    throw 'Install devkitPro with devkitA64, libnx and switch-tools first.'
}
$fixtureDir = Join-Path $projectDir 'romfs\tests'
New-Item -ItemType Directory -Force $fixtureDir | Out-Null
& $compiler -fPIC -O2 -fno-stack-protector -nostdlib -shared '-Wl,--hash-style=sysv' '-Wl,-z,max-page-size=4096' (Join-Path $projectDir 'tests\fixture.c') -o (Join-Path $fixtureDir 'fixture.so')
if ($LASTEXITCODE) { throw 'ELF fixture build failed.' }
& $compiler -fPIC -O2 -fstack-protector-all '-mstack-protector-guard=sysreg' '-mstack-protector-guard-reg=tpidr_el0' '-mstack-protector-guard-offset=40' -nostdlib -shared '-Wl,--hash-style=sysv' '-Wl,-z,max-page-size=4096' (Join-Path $projectDir 'tests\tls_fixture.c') (Join-Path $projectDir 'tests\tls_fixture.S') -o (Join-Path $fixtureDir 'tls-fixture.so')
if ($LASTEXITCODE) { throw 'Android TLS fixture build failed.' }
# Malformed fixtures exercise the same ELF reader used for the game libraries.
[byte[]]$fixtureBytes = [IO.File]::ReadAllBytes((Join-Path $fixtureDir 'fixture.so'))
[IO.File]::WriteAllBytes((Join-Path $fixtureDir 'truncated.so'), [byte[]]$fixtureBytes[0..31])
[byte[]]$badMagic = $fixtureBytes.Clone()
$badMagic[0] = 0
[IO.File]::WriteAllBytes((Join-Path $fixtureDir 'bad-magic.so'), $badMagic)
[byte[]]$badPhdr = $fixtureBytes.Clone()
[BitConverter]::GetBytes([UInt64]::MaxValue).CopyTo($badPhdr, 32)
[IO.File]::WriteAllBytes((Join-Path $fixtureDir 'bad-phdr.so'), $badPhdr)
[byte[]]$badLoad = $fixtureBytes.Clone()
$phdrOffset = [BitConverter]::ToUInt64($badLoad, 32)
$phdrCount = [BitConverter]::ToUInt16($badLoad, 56)
for ($phdrIndex = 0; $phdrIndex -lt $phdrCount; $phdrIndex++) {
    $phdrPosition = [int]($phdrOffset + $phdrIndex * 56)
    if ([BitConverter]::ToUInt32($badLoad, $phdrPosition) -eq 1) {
        [BitConverter]::GetBytes([UInt64]::MaxValue).CopyTo($badLoad, $phdrPosition + 32)
        break
    }
}
[IO.File]::WriteAllBytes((Join-Path $fixtureDir 'bad-load.so'), $badLoad)
$previousPath = $env:PATH
$previousDevkitPro = $env:DEVKITPRO
$previousDevkitA64 = $env:DEVKITA64
try {
    $env:DEVKITPRO = $DevkitPro.Replace('\','/')
    $env:DEVKITA64 = "$env:DEVKITPRO/devkitA64"
    $env:PATH = "${DevkitPro}\msys2\usr\bin;${DevkitPro}\devkitA64\bin;${DevkitPro}\tools\bin;$previousPath"
    # Force all objects to use this build's compiler flags and re-embed RomFS.
    & $make -C $projectDir -B -j2
    if ($LASTEXITCODE) { throw 'Switch build failed.' }
} finally {
    $env:PATH = $previousPath
    $env:DEVKITPRO = $previousDevkitPro
    $env:DEVKITA64 = $previousDevkitA64
}

# Windows native tools may encode Unicode argv with the system codepage.
& python (Join-Path $projectDir tools/fix_nacp_metadata.py) $projectDir
if ($LASTEXITCODE) { throw UTF-8 application metadata verification failed. }
