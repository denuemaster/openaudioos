# OpenAudioOS M0.16 Patch

Apply:

```bash
cd ~/Downloads
unzip OpenAudioOS-M0.16-patch.zip
cp -R OpenAudioOS-M0.16-patch/* ~/Developer/openaudioos/

cd ~/Developer/openaudioos
rm -rf build managed_components dependencies.lock sdkconfig sdkconfig.old

unset IDF_COMPONENT_MANAGER

idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5C4D0377551 flash monitor
```

Test:

- iPhone should still see OpenAudioOS
- Selecting it should not reboot
- Log should show a session ID
- `/fp-setup` should classify as `fply_3_1_1`
- State should become `auth_required`
