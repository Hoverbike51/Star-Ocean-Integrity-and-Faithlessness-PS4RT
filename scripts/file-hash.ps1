# SHA-256 without optional PowerShell module autoloading; works in Windows PowerShell 5.1 and 7.
function Get-So5FileSha256([string]$LiteralPath) {
    $stream = [IO.File]::OpenRead([IO.Path]::GetFullPath($LiteralPath))
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $hash.Dispose(); $stream.Dispose() }
}
