# OpenAudioOS M0.9 Patch

M0.9 enables the ESP-IDF Component Manager because it uses `espressif/mdns`.

Apply:

```bash
cd ~/Downloads
unzip OpenAudioOS-M0.9-patch.zip
cp -R OpenAudioOS-M0.9-patch/* ~/Developer/openaudioos/

cd ~/Developer/openaudioos
rm -rf build managed_components dependencies.lock sdkconfig sdkconfig.old

unset IDF_COMPONENT_MANAGER

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

Test:

- Check serial log for `RAOP mDNS advertised`
- Check serial log for `RTSP placeholder listener started on TCP port 5000`
- Try to see whether iPhone/macOS attempts a connection
- `/api/airplay/status` should show `mdns_started: true` and `rtsp_listener_started: true`

Important:

This is not real AirPlay audio yet. It is the discovery/listener foundation.
