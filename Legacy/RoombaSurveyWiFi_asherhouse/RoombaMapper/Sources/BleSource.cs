using System.Text;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Storage.Streams;

namespace RoombaMapper.Sources;

/// <summary>
/// Connects to the robot's Bluetooth LE "UART" service and listens for notifications.
/// It scans for a device whose advertised name contains the given text (or that
/// advertises the UART service), then subscribes to whichever characteristic in that
/// service supports notifications.
/// </summary>
public sealed class BleSource : DataSourceBase
{
    // Nordic UART Service UUID, used by most BLE "UART" examples.
    // If your firmware uses different UUIDs, change this one line.
    public static readonly Guid UartService = Guid.Parse("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");

    private readonly string _deviceName;
    private BluetoothLEAdvertisementWatcher? _watcher;
    private BluetoothLEDevice? _device;
    private GattDeviceService? _service;
    private GattCharacteristic? _notifyChar;
    private int _connecting;
    private volatile bool _stopped;
    private readonly HashSet<ulong> _seen = new();

    public BleSource(string deviceName) => _deviceName = deviceName;

    public override string Name => $"Bluetooth LE ({_deviceName})";

    public override async Task StartAsync()
    {
        _stopped = false;

        // Check the PC's Bluetooth first so problems give a clear message.
        var adapter = await BluetoothAdapter.GetDefaultAsync();
        if (adapter == null)
            throw new InvalidOperationException("No Bluetooth adapter found on this PC.");
        if (!adapter.IsLowEnergySupported)
            throw new InvalidOperationException("This PC's Bluetooth adapter does not support Bluetooth LE.");
        var radio = await adapter.GetRadioAsync();
        if (radio != null && radio.State != Windows.Devices.Radios.RadioState.On)
            throw new InvalidOperationException("Bluetooth is turned off. Turn it on in Windows Settings.");

        _watcher = new BluetoothLEAdvertisementWatcher { ScanningMode = BluetoothLEScanningMode.Active };
        _watcher.Received += OnAdvertisement;
        _watcher.Stopped += (_, e) =>
        {
            if (!_stopped && e.Error != BluetoothError.Success)
                OnStatus($"Bluetooth scan stopped: {e.Error}");
        };
        _watcher.Start();
        OnStatus($"Scanning for Bluetooth device \"{_deviceName}\"... (every device found is listed in the log)");
    }

    private async void OnAdvertisement(BluetoothLEAdvertisementWatcher watcher,
                                       BluetoothLEAdvertisementReceivedEventArgs args)
    {
        if (_stopped) return;

        var name = args.Advertisement.LocalName ?? "";
        lock (_seen)
        {
            if (name.Length > 0 && _seen.Add(args.BluetoothAddress))
                OnStatus($"Seen: \"{name}\" ({args.RawSignalStrengthInDBm} dBm)");
        }
        bool nameMatch = _deviceName.Length > 0 &&
                         name.Contains(_deviceName, StringComparison.OrdinalIgnoreCase);
        bool serviceMatch = args.Advertisement.ServiceUuids.Contains(UartService);
        if (!nameMatch && !serviceMatch) return;

        if (Interlocked.Exchange(ref _connecting, 1) == 1) return;
        watcher.Stop();

        try
        {
            await ConnectAsync(args.BluetoothAddress, name);
        }
        catch (Exception ex)
        {
            OnStatus("Bluetooth error: " + ex.Message + " (retrying scan)");
            CleanupDevice();
            Interlocked.Exchange(ref _connecting, 0);
            if (!_stopped) try { watcher.Start(); } catch { /* ignore */ }
        }
    }

    private async Task ConnectAsync(ulong address, string name)
    {
        OnStatus($"Found \"{name}\", connecting...");

        _device = await BluetoothLEDevice.FromBluetoothAddressAsync(address)
                  ?? throw new InvalidOperationException("Could not open the device.");

        _device.ConnectionStatusChanged += (d, _) =>
            OnStatus(d.ConnectionStatus == BluetoothConnectionStatus.Connected
                ? "Bluetooth connected"
                : "Bluetooth disconnected");

        var services = await _device.GetGattServicesForUuidAsync(UartService, BluetoothCacheMode.Uncached);
        if (services.Status != GattCommunicationStatus.Success || services.Services.Count == 0)
            throw new InvalidOperationException("UART service not found on the device.");
        _service = services.Services[0];

        var chars = await _service.GetCharacteristicsAsync(BluetoothCacheMode.Uncached);
        if (chars.Status != GattCommunicationStatus.Success)
            throw new InvalidOperationException("Could not read the service's characteristics.");

        _notifyChar = chars.Characteristics.FirstOrDefault(c =>
            c.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Notify))
            ?? throw new InvalidOperationException("No notify characteristic in the UART service.");

        _notifyChar.ValueChanged += (_, e) =>
        {
            var reader = DataReader.FromBuffer(e.CharacteristicValue);
            var bytes = new byte[reader.UnconsumedBufferLength];
            reader.ReadBytes(bytes);
            OnText(Encoding.ASCII.GetString(bytes));
        };

        var status = await _notifyChar.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue.Notify);
        if (status != GattCommunicationStatus.Success)
            throw new InvalidOperationException("Could not turn on notifications.");

        OnStatus($"Bluetooth connected to \"{name}\", receiving data");
    }

    private void CleanupDevice()
    {
        _service?.Dispose();
        _device?.Dispose();
        _service = null;
        _device = null;
        _notifyChar = null;
    }

    public override void Stop()
    {
        _stopped = true;
        try { _watcher?.Stop(); } catch { /* ignore */ }
        _watcher = null;
        CleanupDevice();
    }
}
