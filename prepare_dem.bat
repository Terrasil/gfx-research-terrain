@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "TIFF=%CD%\source-data\usgs\USGS_13_n47w123_20250813.tif"
set "RAW=%CD%\assets\dem\mount_st_helens_usgs_10m_1921_f32.raw"
set "VENV=%CD%\.dem-venv"
set "PYTHON=%VENV%\Scripts\python.exe"

echo.
echo ==========================================
echo Mount St. Helens DEM preparation
echo ==========================================
echo.

if not exist "%TIFF%" (
    echo ERROR: Source TIFF not found:
    echo %TIFF%
    echo.
    pause
    exit /b 1
)

if not exist "%VENV%\Scripts\python.exe" (
    echo Creating local Python environment...

    python -m venv "%VENV%"

    if errorlevel 1 (
        echo.
        echo ERROR: Cannot create Python environment.
        echo Check that Python is installed and available in PATH.
        pause
        exit /b 1
    )
)

echo Updating pip...
"%PYTHON%" -m pip install --upgrade pip

if errorlevel 1 (
    echo.
    echo ERROR: pip update failed.
    pause
    exit /b 1
)

echo.
echo Installing DEM conversion dependencies...
"%PYTHON%" -m pip install --only-binary=:all: numpy rasterio

if errorlevel 1 (
    echo.
    echo ERROR: Could not install numpy/rasterio.
    pause
    exit /b 1
)

echo.
echo Testing Python dependencies...
"%PYTHON%" -c "import numpy, rasterio; print('NumPy:', numpy.__version__); print('Rasterio:', rasterio.__version__)"

if errorlevel 1 (
    echo.
    echo ERROR: numpy/rasterio import failed.
    pause
    exit /b 1
)

echo.
echo Preparing Mount St. Helens DEM...
echo Source:
echo %TIFF%
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass ^
    -File "%CD%\tools\setup_mount_st_helens_dem.ps1" ^
    -Source "%TIFF%" ^
    -Python "%PYTHON%"

if errorlevel 1 (
    echo.
    echo ERROR: DEM conversion failed.
    pause
    exit /b 1
)

if not exist "%RAW%" (
    echo.
    echo ERROR: Expected RAW file was not created:
    echo %RAW%
    pause
    exit /b 1
)

for %%A in ("%RAW%") do set "SIZE=%%~zA"

if not "%SIZE%"=="14760964" (
    echo.
    echo ERROR: Invalid RAW size.
    echo Expected: 14760964 bytes
    echo Actual:   %SIZE% bytes
    pause
    exit /b 1
)

echo.
echo ==========================================
echo DEM PREPARED SUCCESSFULLY
echo ==========================================
echo.
echo Output:
echo %RAW%
echo.
echo Resolution: 1921 x 1921
echo Format:     Float32
echo Spacing:    10 m
echo File size:  %SIZE% bytes
echo.

pause