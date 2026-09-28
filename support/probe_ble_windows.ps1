param(
    [Parameter(Mandatory=$true)][string]$Address,
    [ValidateRange(1,20)][int]$Iterations = 1,
    [switch]$NotificationTest
)
# Discovery against an explicitly identified device. Optional NotificationTest
# creates then removes one labelled local Gadgetbridge notification. No LoRa
# transmission, pairing, configuration changes, or external dependencies.
# Run with Windows PowerShell 5.1 for its built-in WinRT projection.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Devices.Bluetooth.BluetoothLEDevice,Windows.Devices.Bluetooth,ContentType=WindowsRuntime]
$null = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult,Windows.Devices.Bluetooth,ContentType=WindowsRuntime]
$null = [Windows.Devices.Bluetooth.BluetoothCacheMode,Windows.Devices.Bluetooth,ContentType=WindowsRuntime]
$null = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicsResult,Windows.Devices.Bluetooth,ContentType=WindowsRuntime]
$null = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattCommunicationStatus,Windows.Devices.Bluetooth,ContentType=WindowsRuntime]
$null = [Windows.Storage.Streams.DataWriter,Windows.Storage.Streams,ContentType=WindowsRuntime]
$null = [Windows.Storage.Streams.IBuffer,Windows.Storage.Streams,ContentType=WindowsRuntime]
$null = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteOption,Windows.Devices.Bluetooth,ContentType=WindowsRuntime]
if ($NotificationTest) {
    # Keep IBuffer inside typed code: PowerShell 5.1 cannot project the COM
    # object returned by DetachBuffer back into an IBuffer parameter.
    $runtimeDir = Join-Path (Split-Path $PSScriptRoot -Parent) '.pio\xnode-ble-diagnostics'
    $null = New-Item -ItemType Directory -Path $runtimeDir -Force
    $runtimeDll = Join-Path $runtimeDir 'XnodeLocalBleWrite.dll'
    & "$env:windir\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:library "/out:$runtimeDll" "/reference:$env:windir\System32\WinMetadata\Windows.Storage.winmd" "/reference:$env:windir\System32\WinMetadata\Windows.Devices.winmd" "/reference:$env:windir\System32\WinMetadata\Windows.Foundation.winmd" "/reference:$env:windir\Microsoft.NET\Framework64\v4.0.30319\System.Runtime.dll" (Join-Path $PSScriptRoot 'ble_windows_write.cs')
    if ($LASTEXITCODE -ne 0) { throw 'Local WinRT helper compilation failed' }
    Add-Type -Path $runtimeDll
}
$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetGenericArguments().Length -eq 1 -and
    $_.GetParameters().Length -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
} | Select-Object -First 1
function Await-WinRT($operation, [Type]$resultType) {
    $task = $asTask.MakeGenericMethod($resultType).Invoke($null,@($operation))
    if (-not $task.Wait(20000)) { throw 'Bluetooth operation timed out after 20 seconds' }
    return $task.Result
}
$numericAddress = [Convert]::ToUInt64(($Address -replace '[:-]',''),16)
function Write-LocalNotification($characteristic, [string]$json) {
    # DLE clears any incomplete prior frame; GB(...) is the firmware's JSON framing.
    $bytes = [Text.Encoding]::UTF8.GetBytes(([string][char]16) + 'GB(' + $json + ")`n")
    for ($offset=0; $offset -lt $bytes.Length; $offset+=20) {
        $last = [Math]::Min($offset+19,$bytes.Length-1)
        $status = Await-WinRT ([XnodeLocalBleWrite]::Send($characteristic, [byte[]]$bytes[$offset..$last])) ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattCommunicationStatus])
        if ($status.ToString() -ne 'Success') { throw "Local notification write failed: $status" }
    }
}
for ($iteration=1; $iteration -le $Iterations; $iteration++) {
    $device = $null
    $services = $null
    try {
        $device = Await-WinRT ([Windows.Devices.Bluetooth.BluetoothLEDevice]::FromBluetoothAddressAsync($numericAddress)) ([Windows.Devices.Bluetooth.BluetoothLEDevice])
        if ($null -eq $device) { throw 'Device unavailable; no pairing attempted' }
        $services = Await-WinRT ($device.GetGattServicesAsync([Windows.Devices.Bluetooth.BluetoothCacheMode]::Uncached)) ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult])
        [pscustomobject]@{iteration=$iteration; name=$device.Name; status=$services.Status.ToString(); connection=$device.ConnectionStatus.ToString(); service_count=@($services.Services).Count} | ConvertTo-Json -Compress
        if ($services.Status.ToString() -ne 'Success') { throw 'Service discovery failed; no pairing or writes attempted' }
        if ($NotificationTest) {
            $uart = $services.Services | Where-Object { $_.Uuid -eq [guid]'6E400001-B5A3-F393-E0A9-E50E24DCCA9E' } | Select-Object -First 1
            if (-not $uart) { throw 'Existing Gadgetbridge service not found' }
            $characters = Await-WinRT ($uart.GetCharacteristicsForUuidAsync([guid]'6E400002-B5A3-F393-E0A9-E50E24DCCA9E')) ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicsResult])
            if ($characters.Status.ToString() -ne 'Success') { throw 'Gadgetbridge characteristic unavailable' }
            $rx = $characters.Characteristics | Select-Object -First 1
            $diagnosticId = -[Math]::Abs([int64]([guid]::NewGuid().GetHashCode()))
            $cleanup = @{t='notify-'; id=$diagnosticId} | ConvertTo-Json -Compress
            try {
                $notification = @{t='notify'; id=$diagnosticId; src='XNODE DIAGNOSTIC'; title='Local validation'; body='Local Bluetooth notification test. Not a radio message.'} | ConvertTo-Json -Compress
                Write-LocalNotification $rx $notification
                Write-Output "LOCAL_NOTIFICATION_SENT id=$diagnosticId; holding 45 seconds for USB/UI verification"
                Start-Sleep -Seconds 45
            } finally {
                Write-LocalNotification $rx $cleanup
                Write-Output "LOCAL_NOTIFICATION_REMOVAL_SENT id=$diagnosticId"
            }
        }
    } finally {
        if ($services) { foreach ($service in $services.Services) { $service.Dispose() } }
        if ($device) { $device.Dispose() }
    }
    if ($iteration -lt $Iterations) { Start-Sleep -Seconds 3 }
}
