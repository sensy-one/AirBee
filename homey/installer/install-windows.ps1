$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$temporaryDirectory = $null

try {
    Write-Host "SENSY-ONE for Homey`nKeep this PC and Homey on the same network.`n"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $architecture = $env:PROCESSOR_ARCHITEW6432
    if (-not $architecture) { $architecture = $env:PROCESSOR_ARCHITECTURE }
    switch ($architecture) {
        'ARM64' {
            $nodeArchitecture = 'arm64'
            $expectedHash = '8779b1bde1d39f8d420e3b57aa657b39891af434d3de44a919044cec06785921'
        }
        'AMD64' {
            $nodeArchitecture = 'x64'
            $expectedHash = '158f7685b44de51f6c0df1d153526cbcd3e1bc739a8dfc607721cef75de9e541'
        }
        default { throw 'This installer requires 64-bit Windows (Intel/AMD or ARM).' }
    }

    $cacheDirectory = Join-Path $env:LOCALAPPDATA 'SENSY-ONE\HomeyInstaller'
    $nodeName = "node-v24.21.0-win-$nodeArchitecture"
    $nodeDirectory = Join-Path $cacheDirectory $nodeName
    $nodeExecutable = Join-Path $nodeDirectory 'node.exe'
    New-Item -ItemType Directory -Path $cacheDirectory -Force | Out-Null

    if (-not (Test-Path -LiteralPath $nodeExecutable)) {
        Write-Host '1/4 - Downloading the installer tools...'
        $temporaryDirectory = Join-Path $cacheDirectory ([Guid]::NewGuid().ToString())
        New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
        $archive = Join-Path $temporaryDirectory 'node.zip'
        Invoke-WebRequest -UseBasicParsing -Uri "https://nodejs.org/dist/v24.21.0/$nodeName.zip" -OutFile $archive
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) {
            throw 'Download verification failed. Please try again.'
        }
        Expand-Archive -LiteralPath $archive -DestinationPath $temporaryDirectory
        if (Test-Path -LiteralPath $nodeDirectory) { Remove-Item -LiteralPath $nodeDirectory -Recurse -Force }
        Move-Item -LiteralPath (Join-Path $temporaryDirectory $nodeName) -Destination $nodeDirectory
    } else {
        Write-Host '1/4 - Installer tools are ready.'
    }

    $env:PATH = "$nodeDirectory;$env:PATH"
    & $nodeExecutable (Join-Path $PSScriptRoot 'install.cjs') $cacheDirectory
    if ($LASTEXITCODE -ne 0) { throw "Homey installation failed (exit code $LASTEXITCODE)." }
} catch {
    Write-Host "`n$($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    if ($temporaryDirectory -and (Test-Path -LiteralPath $temporaryDirectory)) {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
