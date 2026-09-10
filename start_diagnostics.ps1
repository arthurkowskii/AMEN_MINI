param([switch]$Check)

$ErrorActionPreference = 'Stop'
$Python = Join-Path $PSScriptRoot 'diagnostics\.venv\Scripts\python.exe'
$Requirements = Join-Path $PSScriptRoot 'diagnostics\pc\requirements.txt'
$Application = Join-Path $PSScriptRoot 'diagnostics\pc\amen_diagnostic.py'

if (-not (Test-Path $Python)) {
    if (Get-Command py -ErrorAction SilentlyContinue) {
        & py -3 -m venv (Join-Path $PSScriptRoot 'diagnostics\.venv')
    } elseif (Get-Command python -ErrorAction SilentlyContinue) {
        & python -m venv (Join-Path $PSScriptRoot 'diagnostics\.venv')
    } else {
        throw 'Python 3 avec Tkinter est requis. Installer Python depuis https://www.python.org/downloads/windows/'
    }
    if ($LASTEXITCODE -ne 0) { throw 'Creation de l environnement Python impossible.' }
}

& $Python -c "import importlib.util, sys; spec = importlib.util.find_spec('serial'); sys.exit(0 if spec and __import__('serial').__version__ == '3.5' else 1)"
if ($LASTEXITCODE -ne 0) {
    & $Python -m pip install --requirement $Requirements
    if ($LASTEXITCODE -ne 0) { throw 'Installation de pyserial impossible.' }
}

if ($Check) {
    & $Python -m unittest discover -s (Join-Path $PSScriptRoot 'diagnostics\pc') -p 'test_diagnostic*.py'
    if ($LASTEXITCODE -ne 0) { throw 'Les tests du diagnostic ont echoue.' }
    & $Python $Application --smoke-test
} else {
    & $Python $Application
}
if ($LASTEXITCODE -ne 0) { throw 'Le diagnostic a rencontre une erreur.' }
