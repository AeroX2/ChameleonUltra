![logo](docs/images/ultra-logo.png)

![ultra picture](docs/images/ultra-overview.png)

# ChameleonUltra Authorized Distributors

Lyon, France: [Lab401](https://lab401.com/)

Santa Ana, United States: [Hackerwarehouse](https://hackerwarehouse.com/)

Hastings, UK: [KSEC](https://labs.ksec.co.uk/product/proxgrind-chameleon-ultra/)

Montreal, Canada: [TechSecurityTools](https://techsecuritytools.com/product/chameleon-ultra/)

Shenzhen, China: [Sneaktechnology](https://sneaktechnology.com)

Guangdong, China: [MTools Tec](https://shop.mtoolstec.com/)

Lazada One, Singapore: [Aliexpress by RRG](https://proxgrind.aliexpress.com/store/1101312023)

# What is it and how to use ?

Read the [available documentation](https://github.com/RfidResearchGroup/ChameleonUltra/wiki).

# Custom firmware features (this fork)

This fork includes upstream v2.3.0 plus these changes:

* Long presses fire while held; double-click and A+B chord bindings are configurable.
* Default double-clicks clone LF EM410x (A) or HF UID (B) into a scratch slot.
* A+B toggles BLE advertising and disconnects an active link. The setting survives
  reboot; the SoftDevice is always initialized to avoid BLE-off boot faults.
* Non-blocking boot/wake LED animation and faster button/slot response.
* Persistent HF receiver gain (`hf 14a rxgain`) and LF carrier tuning (`lf tune`).
* LF clone write verification and HID Prox format-candidate enumeration.
* **Indala 64/224-bit PSK1/PSK2** at fc/2, RF/32: raw-frame reading,
  JSON import/export, persistent slot storage, emulation and verified T55xx
  writing through the matching CLI and companion GUI.
  Credential-number/facility-code interpretation is not included.

Indala has synthetic decoder coverage but has **not been verified on physical
hardware**. Validate it with a known Indala tag before relying on its output.
The reader temporarily uses a 125 kHz field and a 166.67 kHz ADC sample clock,
then restores the configured LF frequency and normal sampling path.
See [Indala validation notes](docs/indala.md).

Update the firmware and this fork's CLI together. Upstream allocated command IDs
previously used by the fork, so fork-only button, HF gain and HID candidate
commands now use separate IDs. Older companion GUI builds need matching command
updates for these features. Existing fork v13 settings migrate to v14 while
retaining button bindings, radio state and tuning.

# Compatible applications

* [ChameleonUltraGUI](https://github.com/GameTec-live/ChameleonUltraGUI)
  * [ChameleonUltraGUI (this fork)](https://github.com/AeroX2/ChameleonUltraGUI) — adds a UI to configure this fork's new button bindings (double-click, A+B chord, toggle-BLE, full-read, write)
* [MTools BLE](https://github.com/RfidResearchGroup/ChameleonUltra/wiki/mtoolsble)
* [Mifare Chameleon Tool (iOS only, Beta)](https://apps.apple.com/it/app/mifare-chameleon-tool/id6761231484)
* [Chameleon Ultra (Sailfish OS only)](https://sailfishos-chum.github.io/apps/harbour-chameleon-ultra)
* [Chameleon GO (iOS only)](https://apps.apple.com/us/app/id6802015306)

# Videos

*Beware some of the instructions might have changed since recording, check the current documentation when in doubt!*

* [Downloading and compiling the official CLI](https://www.youtube.com/watch?v=VGpAeitNXH0)
* [Downloading ChameleonUltraGUI](https://www.youtube.com/watch?v=rHH7iqbX3nY)
* [ChameleonUltraGUI features overview](https://www.youtube.com/watch?v=YqE8wyVSse4)
* [Using ChameleonUltraGUI and the Chameleon Ultra](https://www.youtube.com/watch?v=9jtKNJ5-kVY)
* [MTools BLE - How to clone a card with ChameleonUltra](https://youtu.be/IvH-xtdW1Wk?si=4exqgAAeJ-kxU3aN)

# Official channels

Where do you find the community?
* [RFID Hacking community discord server](https://t.ly/d4_C)
  * Software/chameleon-dev for firmware and clients development discussions
  * Devices/chameleon-ultra for usage discussions
* [GameTec_live discord server](https://discord.gg/DJ2A4wxncK)

Other notable projects:
* [Fantasi](https://fantasi.cloud)
  * [Discord](https://fantasi.cloud/discord)
* [chameleon-ultra.js](https://github.com/taichunmin/chameleon-ultra.js/)

###### Searching for the docs repo? Find it [here](https://github.com/RfidResearchGroup/ChameleonUltraDocs)
