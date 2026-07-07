# OpenAudioOS M0.14 Patch

Apply:

```bash
cd ~/Downloads
unzip OpenAudioOS-M0.14-patch.zip
cp -R OpenAudioOS-M0.14-patch/* ~/Developer/openaudioos/

cd ~/Developer/openaudioos
rm -rf build managed_components dependencies.lock sdkconfig sdkconfig.old

unset IDF_COMPONENT_MANAGER

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

Expected:

- Existing AirPlay discovery still works.
- Logs show `AirPlay backend selector initialized`.
- Logs show `Selected stack strategy: rbouteiller_airplay_esp32`.
- No real third-party AirPlay code is compiled yet.

Fetch candidate code separately:

```bash
cd ~/Developer/openaudioos
tools/fetch_airplay_esp32.sh
```

Do this after committing M0.14 if you want third_party code as a separate commit/submodule decision.
