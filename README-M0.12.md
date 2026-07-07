# OpenAudioOS M0.12 Patch

Apply:

```bash
cd ~/Downloads
unzip OpenAudioOS-M0.12-patch.zip
cp -R OpenAudioOS-M0.12-patch/* ~/Developer/openaudioos/

cd ~/Developer/openaudioos
rm -rf build managed_components dependencies.lock sdkconfig sdkconfig.old

unset IDF_COMPONENT_MANAGER

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

Test:

- Select OpenAudioOS from iPhone AirPlay output
- Device should not reboot
- Serial log should show `POST /fp-setup`
- Serial log should show `FP-SETUP summary: ...`
- Watch for the next request after `/fp-setup`
