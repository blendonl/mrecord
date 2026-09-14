param(
    [string]$Version,
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'Programs\mrecord')
)

function Get-MrecordRelease {
    param([string]$Version)
    $tag = if ($Version) { "v$($Version.TrimStart('v'))" } else { $null }
    $endpoint = if ($tag) { "tags/$tag" } else { 'latest' }
    try {
        $found = Invoke-RestMethod "https://api.github.com/repos/blendonl/mrecord/releases/$endpoint"
    } catch {
        throw "Could not find mrecord release $(if ($tag) { $tag } else { 'latest' }): $($_.Exception.Message)"
    }
    $asset = $found.assets | Where-Object name -like 'mrecord-*-win64.zip' | Select-Object -First 1
    if (-not $asset) {
        throw "Release $($found.tag_name) has no mrecord-*-win64.zip to install."
    }
    [pscustomobject]@{ Tag = $found.tag_name; Name = $asset.name; Url = $asset.browser_download_url }
}

function Stop-InstalledMrecord {
    param([string]$Exe)
    $running = @(Get-Process mrecord -ErrorAction SilentlyContinue | Where-Object Path -eq $Exe)
    if (-not $running) {
        return
    }
    & $Exe --stop | Out-Null
    $running | Wait-Process -Timeout 60 -ErrorAction SilentlyContinue
    $running | Where-Object { -not $_.HasExited } | Stop-Process -Force
}

function Send-EnvironmentChange {
    $name = "MRECORD_INSTALL_$PID"
    [Environment]::SetEnvironmentVariable($name, '1', 'User')
    [Environment]::SetEnvironmentVariable($name, [NullString]::Value, 'User')
}

function Add-UserPath {
    param([string]$Directory)
    $Directory = $Directory.TrimEnd('\')
    if (($env:Path -split ';' | ForEach-Object { $_.TrimEnd('\') }) -notcontains $Directory) {
        $env:Path = "$($env:Path.TrimEnd(';'));$Directory"
    }
    $environment = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $true)
    try {
        $entries = @($environment.GetValue('Path', '', 'DoNotExpandEnvironmentNames') -split ';' | Where-Object { $_ })
        $expanded = $entries | ForEach-Object { [Environment]::ExpandEnvironmentVariables($_).TrimEnd('\') }
        if ($expanded -contains $Directory) {
            return $false
        }
        $environment.SetValue('Path', (($entries + $Directory) -join ';'), 'ExpandString')
    } finally {
        $environment.Close()
    }
    Send-EnvironmentChange
    return $true
}

function Install-Mrecord {
    param([string]$Version, [string]$InstallDir)
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue'
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    if (-not [Environment]::Is64BitOperatingSystem) {
        throw 'mrecord is only built for 64-bit Windows.'
    }

    $release = Get-MrecordRelease $Version
    $exe = Join-Path $InstallDir 'mrecord.exe'
    $staging = Join-Path ([IO.Path]::GetTempPath()) "mrecord-install-$([guid]::NewGuid())"
    New-Item -ItemType Directory $staging | Out-Null
    try {
        Write-Host "Downloading $($release.Name)"
        $zip = Join-Path $staging $release.Name
        Invoke-WebRequest $release.Url -OutFile $zip -UseBasicParsing
        Expand-Archive $zip (Join-Path $staging 'unpacked')
        $payload = Get-ChildItem (Join-Path $staging 'unpacked') -Directory | Select-Object -First 1

        Stop-InstalledMrecord $exe
        New-Item -ItemType Directory -Force $InstallDir | Out-Null
        Copy-Item (Join-Path $payload.FullName '*') $InstallDir -Recurse -Force
    } finally {
        Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue
    }

    $addedToPath = Add-UserPath $InstallDir

    Write-Host "Installed mrecord $($release.Tag) to $InstallDir"
    if ($addedToPath) {
        Write-Host "Added it to your PATH. Terminals that were already open need restarting to see it."
    }
    Write-Host "Run 'mrecord --help' to get started."
}

Install-Mrecord -Version $Version -InstallDir $InstallDir
