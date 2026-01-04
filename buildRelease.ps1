$QtPath = "C:\Qt\6.10.1\msvc2022_64"
$BuildDir = "build"
$ExePath = "$BuildDir\Release\lazerdeck.exe"

# Configure
Write-Host "Configuring..."
cmake -S . -B $BuildDir -DCMAKE_PREFIX_PATH=$QtPath
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Build
Write-Host "Building Release..."
cmake --build $BuildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Deploy Qt
Write-Host "Deploying Qt dependencies..."
& "$QtPath\bin\windeployqt.exe" --release $ExePath
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Build complete. Executable is at $ExePath"
