using Windows.Foundation;
using Windows.Storage.Streams;
using Windows.Devices.Bluetooth.GenericAttributeProfile;

// Keep the WinRT IBuffer inside typed code; PowerShell 5.1 does not project it.
public static class XnodeLocalBleWrite {
    public static IAsyncOperation<GattCommunicationStatus> Send(GattCharacteristic characteristic, byte[] bytes) {
        using (var writer = new DataWriter()) {
            writer.WriteBytes(bytes);
            return characteristic.WriteValueAsync(writer.DetachBuffer(), GattWriteOption.WriteWithResponse);
        }
    }
}
